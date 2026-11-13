package org.walkdroid.sony.audio;

import android.app.AlertDialog;
import android.os.Bundle;
import android.os.Handler;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceCategory;
import android.preference.PreferenceScreen;
import android.preference.SwitchPreference;
import android.view.MenuItem;
import java.lang.reflect.Method;
import java.util.LinkedHashMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.walkdroid.audio.exclusive.AppSelectorProtocol;
import org.walkdroid.audio.exclusive.BtCatalogProtocol;
import org.walkdroid.audio.exclusive.BtLinkProtocol;

/** Values always come from the HAL. UI storage is never an apply acknowledgement. */
public final class SonyAudioSettings extends PreferenceActivity {
    private String tr(String text) { return WalkDroidText.translate(this, text); }
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final ExecutorService selectorWorker = Executors.newSingleThreadExecutor();
    private final ExecutorService btCatalogWorker = Executors.newSingleThreadExecutor();
    private final Map<String, Preference> fields = new LinkedHashMap<>();
    private final List<Preference> sections = new ArrayList<>();
    private boolean contentVisible = true;
    private Map<String, String> state;
    private Preference status;
    private Preference player;
    private Preference appSelector;
    private AppSelectorProtocol.State appSelection;
    private boolean selectorBusy;
    private AlertDialog appChooser;
    private Preference btCatalog;
    private boolean btCatalogBusy;
    private BtCatalogProtocol.State btCatalogState;
    private AlertDialog btChooser;
    private SwitchPreference btOutput;private SonyBtOutputService btOutputService;private BtLinkProtocol.State btOutputView;private boolean btOutputBound;
    private Preference btVolume;private SwitchPreference btAbsolute;private AlertDialog btVolumeChoice;
    private SwitchPreference receiver;
    private SonyUsbReceiverService receiverService;
    private SonyUsbReceiverService.ViewState receiverView;
    private boolean receiverBound;
    private SwitchPreference btReceiver;
    private SonyUsbReceiverService btReceiverService;
    private SonyUsbReceiverService.ViewState btReceiverView;
    private boolean btReceiverBound;
    private static final class QuietSwitchPreference extends SwitchPreference {
        QuietSwitchPreference(android.content.Context context){super(context);}
        private void quiet(android.view.View v){v.setSoundEffectsEnabled(false);if(v instanceof android.view.ViewGroup){android.view.ViewGroup g=(android.view.ViewGroup)v;for(int i=0;i<g.getChildCount();++i)quiet(g.getChildAt(i));}}
        @Override protected void onBindView(android.view.View v){super.onBindView(v);quiet(v);}
    }
    private final android.content.ServiceConnection receiverConnection=new android.content.ServiceConnection(){
        @Override public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){receiverService=((SonyUsbReceiverService.LocalBinder)binder).service();renderReceiver();}
        @Override public void onServiceDisconnected(android.content.ComponentName name){receiverService=null;receiverView=null;if(receiver!=null){receiver.setSummary(tr("USB DAC 状态未确认，请重新打开此页读取。"));receiver.setEnabled(false);}}
    };
    private final android.content.ServiceConnection btReceiverConnection=new android.content.ServiceConnection(){
        @Override public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){btReceiverService=((SonyUsbReceiverService.LocalBinder)binder).service();renderReceiver();}
        @Override public void onServiceDisconnected(android.content.ComponentName name){btReceiverService=null;btReceiverView=null;if(btReceiver!=null){btReceiver.setSummary(tr("蓝牙接收状态未确认，请重新打开此页读取。"));btReceiver.setEnabled(false);}}
    };
    private final Runnable refreshReceiver=new Runnable(){@Override public void run(){if(resumed){renderReceiver();main.postDelayed(this,250);}}};
    private final android.content.ServiceConnection btOutputConnection=new android.content.ServiceConnection(){
        @Override public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){btOutputService=((SonyBtOutputService.LocalBinder)binder).service();renderReceiver();}
        @Override public void onServiceDisconnected(android.content.ComponentName name){btOutputService=null;btOutputView=null;if(btOutput!=null){btOutput.setEnabled(false);btOutput.setSummary(tr("蓝牙输出状态未确认，请重新打开此页读取。"));}renderBtVolume();}
    };
    private Preference volumeNote;
    private PreferenceCategory eq, dc, vinyl, dsee, dsd, normalizer, presets;
    private Preference savePreset,loadPreset;
    private AlertDialog presetDialog;
    private Preference dsdMode;
    private Preference dseeStatus;
    private boolean busy;
    private boolean refreshing;
    private boolean resumed;
    private final Handler main = new Handler();
    private final Runnable refreshActive = () -> {
        if (resumed && !busy) {
            // A refresh temporarily disables controls. Doing that while a
            // choice dialog is open can silently discard its user's selection.
            // The native generation/revision check still rejects stale choices.
            if (hasOpenChoice()) scheduleRefresh(); else reload();
        }
    };
    /* The settings row/choice must not create a system click track immediately
     * before the idle-only output-profile transaction. */
    private static final class QuietListPreference extends ListPreference {
        QuietListPreference(android.content.Context context) { super(context); }
        @Override protected void showDialog(Bundle saved) {
            super.showDialog(saved);
            ((AlertDialog) getDialog()).getListView().setSoundEffectsEnabled(false);
        }
    }

    private static Map<String, String> readState() throws Exception {
        String raw = (String) Class.forName("android.media.AudioSystem")
                .getMethod("getParameters", String.class).invoke(null, "sony_state");
        return SonyAudioStatePolicy.parse(raw);
    }
    private static Map<String, String> awaitIdle() throws Exception {
        long deadline = android.os.SystemClock.elapsedRealtime() + 8000;
        Map<String, String> fresh;
        do {
            fresh = readState();
            if (!SonyAudioStatePolicy.current(fresh) || !"1".equals(fresh.get("sony_pending"))) return fresh;
            Thread.sleep(50);
        } while (android.os.SystemClock.elapsedRealtime() < deadline);
        throw new IllegalStateException("声音设置仍在等待确认");
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        setTitle(tr("Sony 声音"));
        if (getActionBar() != null) getActionBar().setDisplayHomeAsUpEnabled(true);
        PreferenceScreen screen = getPreferenceManager().createPreferenceScreen(this);
        setPreferenceScreen(screen);
        getListView().setSoundEffectsEnabled(false);
        status = new Preference(this);
        status.setTitle("Sony Walkman ZX300A");
        status.setSummary(tr("正在读取声音设置…"));
        status.setSelectable(false);
        screen.addPreference(status);
        receiver=new QuietSwitchPreference(this);receiver.setTitle("USB DAC");receiver.setPersistent(false);receiver.setSummary(tr("正在读取 USB DAC 状态…"));receiver.setEnabled(false);
        receiver.setOnPreferenceChangeListener((p,value)->{startForegroundService(new android.content.Intent(this,SonyUsbReceiverService.class).setAction((Boolean)value?SonyUsbReceiverService.ON:SonyUsbReceiverService.OFF));return false;});screen.addPreference(receiver);
        btReceiver=new QuietSwitchPreference(this);btReceiver.setTitle(tr("蓝牙接收"));btReceiver.setPersistent(false);btReceiver.setSummary(tr("正在读取蓝牙接收状态…"));btReceiver.setEnabled(false);
        btReceiver.setOnPreferenceChangeListener((p,value)->{startForegroundService(new android.content.Intent(this,SonyBtReceiverService.class).setAction((Boolean)value?SonyBtReceiverService.ON:SonyBtReceiverService.OFF));return false;});screen.addPreference(btReceiver);
        btCatalog=new Preference(this);btCatalog.setKey("walkdroid_bt_output_device");btCatalog.setPersistent(false);btCatalog.setTitle(tr("蓝牙输出设备"));btCatalog.setSummary(tr("正在读取已配对设备…"));btCatalog.setOnPreferenceClickListener(p->{readBtCatalog(true);return true;});screen.addPreference(btCatalog);
        btOutput=new QuietSwitchPreference(this);btOutput.setKey("walkdroid_bt_output");btOutput.setTitle(tr("蓝牙输出"));btOutput.setPersistent(false);btOutput.setSummary(tr("正在读取蓝牙输出状态…"));btOutput.setEnabled(false);btOutput.setOnPreferenceChangeListener((p,value)->{startForegroundService(new android.content.Intent(this,SonyBtOutputService.class).setAction((Boolean)value?SonyBtOutputService.ON:SonyBtOutputService.OFF));return false;});screen.addPreference(btOutput);
        btVolume=new Preference(this);btVolume.setKey("walkdroid_bt_volume");btVolume.setTitle(tr("蓝牙设备音量"));btVolume.setPersistent(false);btVolume.setEnabled(false);btVolume.setSummary(tr("连接后可调整设备音量"));btVolume.setOnPreferenceClickListener(p->{showBtVolume();return true;});screen.addPreference(btVolume);
        btAbsolute=new QuietSwitchPreference(this);btAbsolute.setKey("walkdroid_bt_absolute_volume");btAbsolute.setTitle(tr("蓝牙绝对音量"));btAbsolute.setPersistent(false);btAbsolute.setEnabled(false);btAbsolute.setOnPreferenceChangeListener((p,value)->{BtLinkProtocol.State expected=btOutputView;if(btOutputService!=null&&expected!=null&&expected.canVolumeMode)btOutputService.volume(expected,(Boolean)value?1:0,true);return false;});screen.addPreference(btAbsolute);
        appSelector=new Preference(this);appSelector.setKey("walkdroid_exclusive_app");appSelector.setPersistent(false);appSelector.setTitle(tr("独占应用"));appSelector.setSummary(tr("正在读取独占状态…"));
        appSelector.setOnPreferenceClickListener(p->{openAppChooser();return true;});screen.addPreference(appSelector);
        player = new Preference(this);player.setTitle(tr("DSD 播放"));player.setSummary(tr("选择 DSF 文件，通过 3.5 mm 输出 DSD 转 PCM。"));
        player.setOnPreferenceClickListener(p -> {startActivity(new android.content.Intent(this,SonyDsfPlayerActivity.class));return true;});screen.addPreference(player);
        addSwitch(screen, "sony_direct", tr("直出"), tr("按原厂直出行为处理；关闭后使用下方音效。"));
        presets=category(tr("保存的声音设置"));
        savePreset=new Preference(this);savePreset.setKey("sony_sound_preset_save");savePreset.setPersistent(false);
        savePreset.setTitle(tr("保存当前声音设置"));savePreset.setSummary(tr("保存至三组设置之一，包含均衡器、音调、DSEE HX、DC 相位、黑胶和动态音量均衡。"));
        savePreset.setOnPreferenceClickListener(p->{choosePreset(true);return true;});presets.addPreference(savePreset);
        loadPreset=new Preference(this);loadPreset.setKey("sony_sound_preset_load");loadPreset.setPersistent(false);
        loadPreset.setTitle(tr("载入声音设置"));loadPreset.setSummary(tr("载入已保存的音效；保持当前音量、增益、直出、DSD 和 Walkman One 播放模式。"));
        loadPreset.setOnPreferenceClickListener(p->{choosePreset(false);return true;});presets.addPreference(loadPreset);
        PreferenceCategory output = category(tr("耳机输出"));
        String[] volumeLabels = new String[121], volumeValues = new String[121];
        for (int i = 0; i <= 120; ++i) {volumeValues[i] = Integer.toString(i);volumeLabels[i] = i + " / 120";}
        addList(output, "sony_volume", tr("Walkman 音量"), volumeLabels, volumeValues);
        addSwitch(output, "sony_avls", tr("AVLS 音量限制"), tr("按原厂当前输出与增益限制音量；关闭时不会自动提高音量。"));
        addSwitch(output, "sony_se_gain", tr("3.5 mm 高增益"), tr("切换时按原厂规则调整音量；低于目标初始音量时保持原值。"));
        addSwitch(output, "sony_btl_gain", tr("4.4 mm 高增益"), tr("4.4 mm 平衡输出尚未完成。"));
        Preference nativeDsd = new Preference(this);
        nativeDsd.setTitle(tr("原生 DSD"));nativeDsd.setSummary(tr("尚未完成；目前可使用 3.5 mm 的 DSD 转 PCM。"));
        nativeDsd.setEnabled(false);output.addPreference(nativeDsd);
        String[] balanceLabels = new String[89], balanceValues = new String[89];
        for (int i = 0; i <= 88; ++i) {balanceValues[i] = Integer.toString(i);balanceLabels[i] = String.format(Locale.ROOT, "%.1f dB", -i * 0.5);}
        addList(output, "sony_balance_l", tr("左声道衰减"), balanceLabels, balanceValues);
        addList(output, "sony_balance_r", tr("右声道衰减"), balanceLabels, balanceValues);
        volumeNote = new Preference(this);
        volumeNote.setSelectable(false);output.addPreference(volumeNote);
        PreferenceCategory cfw = category("Walkman One");
        addList(cfw, "sony_cfw_profile", tr("播放模式"),
                new String[]{"Normal", "Plus v1", "Plus v2"}, new String[]{"0", "1", "2"});
        addList(cfw, "sony_cfw_signature", tr("CFW 调音取向"),
                new String[]{tr("未指定（保持当前链路）"), "Neutral", "Warm（Midnight v2）", "Bright（Dawn v2.1）", "WM1Z"},
                new String[]{"-1", "0", "1", "2", "3"});
        Preference signatureNote = new Preference(this);
        signatureNote.setSelectable(false);
        signatureNote.setSummary(tr("实验性选项：完整调音效果仍在验证。停止播放后可切换。"));
        cfw.addPreference(signatureNote);
        addList(cfw, "sony_cfw_gain", tr("CFW 音量模式（GMD）"),
                new String[]{tr("Normal（标准）"), tr("Higher（较高）"), tr("Lower（较低）")}, new String[]{"0", "1", "2"});
        dsd = category(tr("DSD 播放设置"));
        dsdMode = new Preference(this);dsdMode.setTitle(tr("当前 DSD 处理"));dsdMode.setSelectable(false);dsd.addPreference(dsdMode);
        addList(dsd, "sony_dsd_filter", tr("DSD 滤波器"), new String[]{tr("缓降（Slow Roll-Off）"), tr("陡降（Sharp Roll-Off）")}, new String[]{"1", "2"});
        addList(dsd, "sony_dsd_gain", tr("DSD 增益"), new String[]{"0 dB", "−3 dB"}, new String[]{"1", "2"});
        eq = category(tr("均衡器与音调"));
        addList(eq, "sony_selector", tr("调整方式"), new String[]{tr("十段均衡器"), tr("三段音调")}, new String[]{"2", "3"});
        addSwitch(eq, "sony_eq", tr("十段均衡器"), tr("选择十段均衡器并关闭直出后生效。"));
        for (int i = 0; i < 10; ++i) addGain(eq, "sony_band" + i, tr("频段 ") + (i + 1));
        addSwitch(eq, "sony_tone", tr("音调控制"), tr("选择三段音调并关闭直出后生效。"));
        addGain(eq, "sony_low", tr("低音"));
        addGain(eq, "sony_mid", tr("中音"));
        addGain(eq, "sony_high", tr("高音"));
        dsee = category("DSEE HX");
        addSwitch(dsee, "sony_dsee", "DSEE HX", tr("使用 Sony 原厂升频处理，直出开启时不适用。"));
        addList(dsee, "sony_dsee_mode", tr("DSEE 模式"),
                new String[]{tr("标准"), tr("女声"), tr("男声"), tr("打击乐"), tr("弦乐")},
                new String[]{"1", "2", "3", "4", "5"});
        dseeStatus=new Preference(this);dseeStatus.setTitle(tr("当前处理"));dseeStatus.setSelectable(false);dsee.addPreference(dseeStatus);
        dc = category(tr("DC 相位线性器"));
        addSwitch(dc, "sony_dc", tr("DC 相位线性器"), tr("使用原厂相位处理。"));
        addList(dc, "sony_dc_mode", tr("相位模式"),
                new String[]{tr("类型 A LOW"), tr("类型 A STANDARD"), tr("类型 A HIGH"), tr("类型 B LOW"), tr("类型 B STANDARD"), tr("类型 B HIGH")},
                new String[]{"1", "2", "3", "4", "5", "6"});
        vinyl = category(tr("黑胶处理"));
        addSwitch(vinyl, "sony_vinyl", tr("黑胶处理"), tr("使用原厂黑胶处理效果。"));
        addList(vinyl, "sony_vinyl_mode", tr("黑胶模式"),
                new String[]{tr("标准"), tr("音臂共振"), tr("转盘共振"), tr("音纹杂音")},
                new String[]{"1", "2", "3", "4"});
        normalizer = category(tr("动态音量均衡"));
        addSwitch(normalizer, "sony_normalizer", tr("动态音量均衡"), tr("使用 Sony 原厂处理，减小曲目间的音量差异；直出开启时不生效。"));
        Preference note = new Preference(this);
        note.setTitle(tr("当前支持范围"));
        note.setSummary(tr("USB DAC 和蓝牙接收独占声音输出，开启前请连接耳机并调整音量。DSEE AI 适用于 44.1 / 48 kHz 立体声 PCM；其他格式遵循原厂处理规则。4.4 mm 与原生 DSD 尚未完成，更多 CFW 调音取向仍在适配。"));
        note.setSelectable(false);screen.addPreference(note);
        for (int i = 0; i < screen.getPreferenceCount(); ++i) {
            Preference p = screen.getPreference(i);p.setOrder(i);if (p != status&&p!=receiver&&p!=btReceiver&&p!=btCatalog&&p!=btOutput&&p!=btVolume&&p!=btAbsolute) sections.add(p);
        }
        showContent(false);
        updateVisibility();setBusy(true);
        // onResume starts the first read; unknown state never exposes effects.
        busy = false;
    }
    @Override public void onResume() { super.onResume(); resumed = true;
        if(!receiverBound){android.content.Intent i=new android.content.Intent(this,SonyUsbReceiverService.class).setAction(SonyUsbReceiverService.REFRESH);receiverBound=bindService(i,receiverConnection,BIND_AUTO_CREATE);}
        if(!btReceiverBound){android.content.Intent i=new android.content.Intent(this,SonyBtReceiverService.class).setAction(SonyBtReceiverService.REFRESH);btReceiverBound=bindService(i,btReceiverConnection,BIND_AUTO_CREATE);}
        if(!btOutputBound){android.content.Intent i=new android.content.Intent(this,SonyBtOutputService.class).setAction(SonyBtOutputService.REFRESH);btOutputBound=bindService(i,btOutputConnection,BIND_AUTO_CREATE);}
        main.removeCallbacks(refreshReceiver);main.post(refreshReceiver);if (!busy) reload();refreshSelector();readBtCatalog(false); }
    @Override public void onPause() { resumed = false; main.removeCallbacks(refreshActive);main.removeCallbacks(refreshReceiver);if(receiverBound){unbindService(receiverConnection);receiverBound=false;receiverService=null;}if(btReceiverBound){unbindService(btReceiverConnection);btReceiverBound=false;btReceiverService=null;}if(btOutputBound){unbindService(btOutputConnection);btOutputBound=false;btOutputService=null;}super.onPause(); }
    @Override public void onDestroy() { main.removeCallbacks(refreshActive); worker.shutdown();selectorWorker.shutdown();btCatalogWorker.shutdown(); super.onDestroy(); }
    @Override public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == android.R.id.home) { finish(); return true; }
        return super.onOptionsItemSelected(item);
    }
    private PreferenceCategory category(String title) {
        PreferenceCategory category = new PreferenceCategory(this);
        category.setTitle(title);getPreferenceScreen().addPreference(category);return category;
    }
    private void bind(Preference p, String key) {
        p.setKey(key);p.setPersistent(false);fields.put(key, p);
        p.setOnPreferenceChangeListener((pref, value) -> {
            String v = value instanceof Boolean ? ((Boolean) value ? "1" : "0") : value.toString();
            apply(key, v);return false;
        });
    }
    private void addSwitch(android.preference.PreferenceGroup group, String key, String title, String summary) {
        SwitchPreference p = new SwitchPreference(this);p.setTitle(title);p.setSummary(summary);bind(p, key);group.addPreference(p);
    }
    private void addList(android.preference.PreferenceGroup group, String key, String title, String[] labels, String[] values) {
        ListPreference p = new QuietListPreference(this);p.setTitle(title);p.setEntries(labels);p.setEntryValues(values);bind(p, key);group.addPreference(p);
    }
    private void addGain(android.preference.PreferenceGroup group, String key, String title) {
        String[] labels = new String[41], values = new String[41];
        for (int i = 0; i < 41; ++i) {values[i] = Integer.toString(i - 20);labels[i] = String.format(Locale.ROOT, "%+.1f dB", (i - 20) * 0.5);}
        addList(group, key, title, labels, values);
    }
    private void setBusy(boolean value) {
        busy = value;
        if (state == null && eq != null) updateVisibility();
        for (Map.Entry<String, Preference> entry : fields.entrySet()) {
            String key = entry.getKey();
            boolean enabled = !value && !"sony_btl_gain".equals(key) && SonyAudioStatePolicy.editable(state, key);
            entry.getValue().setEnabled(enabled);
        }
        if(player!=null)player.setEnabled(!value);
        if(appSelector!=null)appSelector.setEnabled(!selectorBusy);
        if(btCatalog!=null)btCatalog.setEnabled(!btCatalogBusy);
        boolean presetReady=!value&&SonyAudioStatePolicy.presetEditable(state);
        if(savePreset!=null)savePreset.setEnabled(presetReady);
        if(loadPreset!=null)loadPreset.setEnabled(presetReady);
    }
    private void updateVisibility() {
        if(presets!=null){
            attach(getPreferenceScreen(),presets,contentVisible&&state!=null);
        }
        boolean effects = contentVisible && SonyAudioStatePolicy.effectsVisible(state);
        for (PreferenceCategory group : new PreferenceCategory[]{eq, dsee, dc, vinyl, normalizer}) {
            attach(getPreferenceScreen(),group,group == normalizer ? contentVisible && SonyAudioStatePolicy.normalizerVisible(state) : effects);
        }
        attach(getPreferenceScreen(),dsd,contentVisible && state != null && "dsf".equals(state.get("sony_source")));
        for (String key : new String[]{"sony_dsd_filter", "sony_dsd_gain"}) {
            Preference p = fields.get(key);
            attach(dsd,p,!SonyAudioStatePolicy.nativeDsd(state));
        }
    }
    private static void attach(android.preference.PreferenceGroup parent,Preference child,boolean visible){
        boolean attached=false;
        for(int i=0;i<parent.getPreferenceCount();++i)if(parent.getPreference(i)==child){attached=true;break;}
        // Removing an absent Preference still notifies the Android adapter.
        // Preserve the hierarchy during unchanged periodic observations.
        if(visible&&!attached)parent.addPreference(child);
        else if(!visible&&attached)parent.removePreference(child);
    }
    private void scheduleRefresh() {
        main.removeCallbacks(refreshActive);
        // An external owner can start while this page is idle. Reads are now
        // readonly and bounded; no route is opened merely to refresh the page.
        if (resumed) main.postDelayed(refreshActive, 1000);
    }
    private void showContent(boolean visible) {
        if (contentVisible == visible) return;
        contentVisible = visible;
        if (!visible) dismissChoices();
        getPreferenceScreen().removeAll();getPreferenceScreen().addPreference(status);getPreferenceScreen().addPreference(receiver);getPreferenceScreen().addPreference(btReceiver);
        // Transport ownership is observed by its own authenticated service.
        // A busy/unavailable wired-DSP read must never hide its OFF control.
        getPreferenceScreen().addPreference(btCatalog);getPreferenceScreen().addPreference(btOutput);getPreferenceScreen().addPreference(btVolume);getPreferenceScreen().addPreference(btAbsolute);
        if (visible) for (Preference p : sections) getPreferenceScreen().addPreference(p);
    }
    private void dismissChoices() {
        if(appChooser!=null&&appChooser.isShowing())appChooser.dismiss();
        if(btChooser!=null&&btChooser.isShowing())btChooser.dismiss();
        if(btVolumeChoice!=null&&btVolumeChoice.isShowing())btVolumeChoice.dismiss();
        for (Preference p : fields.values()) {
            if (p instanceof ListPreference) {
                android.app.Dialog dialog = ((ListPreference) p).getDialog();
                if (dialog != null && dialog.isShowing()) dialog.dismiss();
            }
        }
    }
    private boolean hasOpenChoice() {
        if(presetDialog!=null&&presetDialog.isShowing())return true;
        if(appChooser!=null&&appChooser.isShowing())return true;
        if(btChooser!=null&&btChooser.isShowing())return true;
        if(btVolumeChoice!=null&&btVolumeChoice.isShowing())return true;
        for (Preference p : fields.values()) {
            if (p instanceof ListPreference) {
                android.app.Dialog dialog = ((ListPreference) p).getDialog();
                if (dialog != null && dialog.isShowing()) return true;
            }
        }
        return false;
    }
    private void showState(Map<String, String> fresh) {
        if (fresh == null || fresh.isEmpty()) {
            state = null;showContent(false);status.setSummary(btOutputActive()?tr("蓝牙输出 · ")+tr(btOutputView.message):tr(SonyAudioStatePolicy.status(fresh)));scheduleRefresh();return;
        }
        state = fresh;
        if (!SonyAudioStatePolicy.editable(fresh)) dismissChoices();
        showContent(true);
        updateVisibility();
        boolean aiAvailable=SonyDseeState.available(fresh);
        dsee.setTitle(aiAvailable?"DSEE HX / AI":"DSEE HX");
        fields.get("sony_dsee").setTitle(aiAvailable?"DSEE HX / AI":"DSEE HX");
        ListPreference dseeModes=(ListPreference)fields.get("sony_dsee_mode");
        String[] dseeLabels={aiAvailable?"AI（44.1 / 48 kHz）":tr("标准"),tr("女声"),tr("男声"),tr("打击乐"),tr("弦乐")};
        if(!java.util.Arrays.equals(dseeModes.getEntries(),dseeLabels))dseeModes.setEntries(dseeLabels);
        dseeStatus.setSummary(tr(SonyDseeState.summary(fresh)));
        for (Map.Entry<String, Preference> entry : fields.entrySet()) {
            String value = fresh.get(entry.getKey());Preference p = entry.getValue();
            if (p instanceof SwitchPreference) ((SwitchPreference) p).setChecked("1".equals(value));
            else if (p instanceof ListPreference) {ListPreference list = (ListPreference) p;list.setValue(value);list.setSummary(list.getEntry() != null ? list.getEntry() : tr("请选择模式"));}
        }
        status.setSummary(tr(SonyAudioStatePolicy.status(fresh)));
        dsdMode.setSummary(SonyAudioStatePolicy.nativeDsd(fresh) ? tr("原生 DSD · 4.4 mm") : tr("Sony 原厂 DSD 转 PCM"));
        ListPreference profile = (ListPreference) fields.get("sony_cfw_profile");
        profile.setSummary((profile.getEntry() == null ? tr("未读取模式") : profile.getEntry()) + tr(" · 停止播放后可切换"));
        ListPreference gain = (ListPreference) fields.get("sony_cfw_gain");
        String actual = fresh.get("sony_cfw_gain_applied");
        String actualLabel = "0".equals(actual) ? tr("Normal（标准）") : "1".equals(actual) ? tr("Higher（较高）") : "2".equals(actual) ? tr("Lower（较低）") : tr("未确认");
        gain.setSummary((actual != null && actual.equals(gain.getValue()) ? tr("当前生效：") + actualLabel : tr("已保存：") + gain.getEntry() + tr("；当前生效：") + actualLabel)
                + tr("。更改后需重新启动，与耳机高增益开关分别设置。"));
        scheduleRefresh();
        boolean external = "1".equals(fresh.get("sony_readonly"));
        boolean outputOpen = external || "1".equals(fresh.get("sony_active"));
        boolean balanced = "1".equals(fresh.get(outputOpen ? "sony_output" : "sony_last_headphone"));
        volumeNote.setSummary((outputOpen ? tr("当前输出：") : tr("上次播放输出："))
                + (balanced ? "4.4 mm" : "3.5 mm") + " · "
                + ("1".equals(fresh.get(balanced ? "sony_btl_gain" : "sony_se_gain")) ? tr("高增益") : tr("普通增益"))
                + (SonyAudioStatePolicy.btReceiver(fresh)||SonyAudioStatePolicy.usbDac(fresh) ? (SonyAudioStatePolicy.editable(fresh,"sony_volume")?tr("。可调整原厂音量；其他声音设置请先关闭接收。"):tr("。声音设置请在关闭接收后修改。"))
                    : external ? tr("。DSF 使用原厂音量；未经过 Android 媒体混音与软件音量。") : tr("。播放器音量和 Android 媒体音量共同决定响度。")));
        Preference avls = fields.get("sony_avls");
        avls.setSummary("1".equals(fresh.get("sony_avls")) ? tr("已开启 · 当前上限 ") + fresh.get("sony_avls_limit") + " / 120" : tr("按原厂当前输出与增益限制音量；关闭时不会自动提高音量。"));
    }
    private void reload() {
        if(busy||refreshing)return;
        refreshing=true;final boolean initial=state==null;
        if(initial)setBusy(true);
        worker.execute(() -> {
            try {Map<String, String> fresh = awaitIdle();runOnUiThread(() -> {
                refreshing=false;if(isFinishing())return;
                // A background observation must not disable a user's tap or
                // replace the state/receipt of a write admitted after it.
                if(!initial&&busy)return;
                if(fresh==null||!fresh.equals(state))showState(fresh);
                setBusy(false);
            });}
            catch (Exception e) {runOnUiThread(() -> {
                refreshing=false;if(isFinishing()||!initial&&busy)return;showState(null);setBusy(false);
            });}
        });
    }
    private void renderReceiver(){
        boolean exclusive=receiverExclusive(),changed=false;
        if(receiverService!=null&&receiver!=null){SonyUsbReceiverService.ViewState fresh=receiverService.state();if(fresh!=receiverView){receiverView=fresh;changed=true;if(fresh.known)receiver.setChecked(fresh.checked);receiver.setTitle(fresh.known?"USB DAC":tr("USB DAC（状态待确认）"));receiver.setSummary(tr(fresh.message));}}
        if(btReceiverService!=null&&btReceiver!=null){SonyUsbReceiverService.ViewState fresh=btReceiverService.state();if(fresh!=btReceiverView){btReceiverView=fresh;changed=true;if(fresh.known)btReceiver.setChecked(fresh.checked);btReceiver.setTitle(fresh.known?tr("蓝牙接收"):tr("蓝牙接收（状态待确认）"));btReceiver.setSummary(tr(fresh.message));}}
        if(btOutputService!=null&&btOutput!=null){BtLinkProtocol.State fresh=btOutputService.state();if(fresh!=btOutputView){btOutputView=fresh;changed=true;btOutput.setChecked(fresh.connected||fresh.busy);btOutput.setSummary(tr(fresh.message));btOutput.setEnabled(fresh.connected||fresh.busy||fresh.canConnect&&!receiverExclusive());if(btCatalog!=null)btCatalog.setEnabled(!btCatalogBusy&&!receiverExclusive()&&!btOutputActive());if(appSelector!=null)appSelector.setEnabled(!selectorBusy&&!receiverExclusive()&&!btOutputActive());}}
        if(!changed)return;renderBtVolume();
        receiver.setEnabled(receiverView!=null&&receiverView.toggleEnabled&&(btReceiverView==null||!btReceiverView.exclusive));
        btReceiver.setEnabled(btReceiverView!=null&&btReceiverView.toggleEnabled&&(receiverView==null||!receiverView.exclusive));
        boolean active=receiverExclusive();if(exclusive!=active){if(active)dismissChoices();setBusy(busy);if(!busy&&resumed)reload();}
        if(!busy&&btReceiverView!=null&&btReceiverView.exclusive){status.setSummary(SonyAudioStatePolicy.current(state)&&SonyAudioStatePolicy.btReceiver(state)?tr(SonyAudioStatePolicy.status(state)):tr(btReceiverView.message)+tr(" 声音设置请在关闭接收后修改。"));}
    }
    private boolean receiverExclusive(){return receiverView!=null&&receiverView.exclusive||btReceiverView!=null&&btReceiverView.exclusive;}
    private boolean btOutputActive(){return btOutputView!=null&&(btOutputView.connected||btOutputView.busy);}
    private void renderBtVolume(){
        if(btVolume==null||btAbsolute==null)return;BtLinkProtocol.State s=btOutputView;
        btVolume.setSummary(s==null?tr("连接后可调整设备音量"):tr(s.volumeMessage));btVolume.setEnabled(btOutputService!=null&&s!=null&&s.canVolume);
        boolean known=s!=null&&s.connected&&s.volume.has(BtLinkProtocol.Volume.MODE);
        btAbsolute.setChecked(known&&s.volume.mode==1);btAbsolute.setEnabled(btOutputService!=null&&s!=null&&s.canVolumeMode);
        btAbsolute.setSummary(!known?tr("连接后读取设备状态"):s.volume.mode==0?tr("按原厂方式发送相对音量指令"):s.volume.absolute()?tr("按设备回报音量调节，等待设备确认"):tr("已开启；当前连接按原厂相对音量方式调节"));
        if(btVolumeChoice!=null&&btVolumeChoice.isShowing()&&(s==null||!s.canVolume||receiverExclusive()))btVolumeChoice.dismiss();
    }
    private void showBtVolume(){
        final BtLinkProtocol.State expected=btOutputView;if(btOutputService==null||expected==null||!expected.canVolume)return;
        btVolumeChoice=new AlertDialog.Builder(this).setTitle(tr("蓝牙设备音量")).setMessage(tr(expected.volumeMessage))
            .setNegativeButton(tr("降低"),(dialog,which)->{if(btOutputService!=null)btOutputService.volume(expected,-1,false);})
            .setPositiveButton(tr("提高"),(dialog,which)->{if(btOutputService!=null)btOutputService.volume(expected,1,false);})
            .setNeutralButton(tr("取消"),null).create();btVolumeChoice.show();
        for(int which:new int[]{AlertDialog.BUTTON_NEGATIVE,AlertDialog.BUTTON_POSITIVE,AlertDialog.BUTTON_NEUTRAL})btVolumeChoice.getButton(which).setSoundEffectsEnabled(false);
    }
    private void renderBtCatalog(BtCatalogProtocol.State s){
        btCatalogState=s;
        if(s==null)btCatalog.setSummary(tr("设备状态未确认，点按重新读取"));
        else if(!s.known)btCatalog.setSummary(tr(s.message));
        else if(s.selected.isEmpty())btCatalog.setSummary(tr("未选择 · 已配对设备 ")+s.peers.length+tr(" 台"));
        else{String label=s.selected;for(BtCatalogProtocol.Peer p:s.peers)if(p.address.equals(s.selected))label=p.name.isEmpty()?p.address:p.name;btCatalog.setSummary(label+tr(" · 已选择"));}
        btCatalog.setEnabled(!btCatalogBusy&&!receiverExclusive()&&!btOutputActive());
    }
    private void readBtCatalog(boolean choose){
        if(btCatalogBusy||receiverExclusive()||btOutputActive())return;btCatalogBusy=true;btCatalog.setEnabled(false);
        btCatalogWorker.execute(()->{
            BtCatalogProtocol.State fresh=null;try{fresh=new SonyBtCatalogClient().state();}catch(Exception ignored){}
            final BtCatalogProtocol.State s=fresh;runOnUiThread(()->{btCatalogBusy=false;if(isFinishing()||!resumed)return;renderBtCatalog(s);
                if(!choose||s==null||!s.known||receiverExclusive())return;
                String[] labels=new String[s.peers.length+1];labels[0]=tr("不指定设备");int checked=s.selected.isEmpty()?0:-1;
                for(int n=0;n<s.peers.length;++n){BtCatalogProtocol.Peer p=s.peers[n];labels[n+1]=(p.name.isEmpty()?tr("蓝牙设备"):p.name)+"\n"+p.address;if(p.address.equals(s.selected))checked=n+1;}
                btChooser=new AlertDialog.Builder(this).setTitle(tr("蓝牙输出设备")).setSingleChoiceItems(labels,checked,(dialog,n)->{dialog.dismiss();saveBtTarget(s,n==0?"":s.peers[n-1].address);}).setNegativeButton(tr("取消"),null).create();btChooser.show();btChooser.getListView().setSoundEffectsEnabled(false);
            });
        });
    }
    private void saveBtTarget(BtCatalogProtocol.State expected,String address){
        if(btCatalogBusy||receiverExclusive()||btOutputActive())return;btCatalogBusy=true;btCatalog.setEnabled(false);btCatalog.setSummary(tr("正在保存设备选择…"));
        btCatalogWorker.execute(()->{BtCatalogProtocol.State fresh=null;try{fresh=new SonyBtCatalogClient().select(expected,address);}catch(Exception ignored){}
            final BtCatalogProtocol.State s=fresh;runOnUiThread(()->{btCatalogBusy=false;if(!isFinishing())renderBtCatalog(s);});
        });
    }
    private void renderSelector(AppSelectorProtocol.State fresh){
        appSelection=fresh;
        if(fresh==null)appSelector.setSummary(tr("独占状态未确认，点按重新读取"));
        else if(!fresh.known||fresh.pending||fresh.error!=0)appSelector.setSummary(tr(fresh.message));
        else if(fresh.uid==-1)appSelector.setSummary(tr("未指定 · 普通播放"));
        else {
            String label=fresh.packageName;
            try{label=getPackageManager().getApplicationLabel(getPackageManager().getApplicationInfo(fresh.packageName,0)).toString();}catch(Exception ignored){}
            appSelector.setSummary(label+tr(" · 已独占"));
        }
        appSelector.setEnabled(!selectorBusy&&!receiverExclusive()&&!btOutputActive());
    }
    private void refreshSelector(){
        if(selectorBusy)return;selectorBusy=true;
        selectorWorker.execute(()->{AppSelectorProtocol.State fresh=null;try{fresh=new SonyAppSelectorClient().state();}catch(Exception ignored){}
            final AppSelectorProtocol.State current=fresh;runOnUiThread(()->{selectorBusy=false;if(!isFinishing())renderSelector(current);});});
    }
    private void openAppChooser(){
        if(selectorBusy||receiverExclusive()||btOutputActive())return;selectorBusy=true;appSelector.setEnabled(false);appSelector.setSummary(tr("正在读取应用…"));
        selectorWorker.execute(()->{
            try{SonyAppSelectorClient.Listing list=new SonyAppSelectorClient().list();
                runOnUiThread(()->{selectorBusy=false;if(isFinishing()||!resumed)return;renderSelector(list.state);
                    if(!list.state.known||list.state.pending)return;
                    String[] labels=new String[list.choices.length+1];labels[0]=tr("不指定（普通播放）");int checked=list.state.uid==-1?0:-1;
                    for(int i=0;i<list.choices.length;i++){labels[i+1]=list.choices[i].label+"\n"+list.choices[i].packageName;if(list.choices[i].packageName.equals(list.state.packageName))checked=i+1;}
                    appChooser=new AlertDialog.Builder(this).setTitle(tr("独占应用")).setSingleChoiceItems(labels,checked,(dialog,index)->{dialog.dismiss();
                        AppSelectorProtocol.Choice choice=index==0?null:list.choices[index-1];applyAppSelection(choice,list.state.generation);
                    }).setNegativeButton(tr("取消"),null).create();appChooser.show();appChooser.getListView().setSoundEffectsEnabled(false);
                });
            }catch(Exception e){runOnUiThread(()->{selectorBusy=false;if(!isFinishing())renderSelector(null);});}
        });
    }
    private void applyAppSelection(AppSelectorProtocol.Choice choice,long generation){
        if(selectorBusy||receiverExclusive()||btOutputActive())return;selectorBusy=true;appSelector.setEnabled(false);appSelector.setSummary(tr("正在切换独占应用…"));
        selectorWorker.execute(()->{AppSelectorProtocol.State fresh=null;
            try{fresh=new SonyAppSelectorClient().select(choice==null?"":choice.packageName,choice==null?"":choice.token,generation);}catch(Exception ignored){}
            final AppSelectorProtocol.State current=fresh;runOnUiThread(()->{selectorBusy=false;if(isFinishing())return;renderSelector(current);if(!busy)reload();});
        });
    }
    private void choosePreset(boolean save){
        if(busy||!SonyAudioStatePolicy.presetEditable(state))return;
        presetDialog=new AlertDialog.Builder(this).setTitle(save?tr("保存当前声音设置"):tr("载入声音设置"))
            .setItems(new String[]{tr("声音设置 1"),tr("声音设置 2"),tr("声音设置 3")},(dialog,which)->applyPreset(save,which+1))
            .setNegativeButton(tr("取消"),null).create();
        presetDialog.setOnDismissListener(dialog->presetDialog=null);presetDialog.show();
        presetDialog.getListView().setSoundEffectsEnabled(false);
    }
    private void applyPreset(boolean save,int slot){
        if(busy||slot<1||slot>3||!SonyAudioStatePolicy.presetEditable(state))return;
        final String revision=state.get("sony_revision");
        setBusy(true);status.setSummary(save?tr("正在保存声音设置…"):tr("正在载入声音设置…"));
        worker.execute(()->{
            try{
                String request="sony_revision="+revision+";sony_preset_"+(save?"save":"load")+"="+slot;
                int result=(Integer)Class.forName("android.media.AudioSystem").getMethod("setParameters",String.class).invoke(null,request);
                if(result!=0)throw new IllegalStateException(tr("预设请求失败（")+result+"）");
                Map<String,String> observed;try{observed=readState();}catch(Exception ignored){observed=null;}
                final Map<String,String> fresh=observed;
                runOnUiThread(()->{if(isFinishing())return;showState(fresh);setBusy(false);
                    status.setSummary((save?tr("保存请求已发送：声音设置 "):tr("载入请求已发送：声音设置 "))+slot);});
            }catch(Exception error){
                runOnUiThread(()->{if(isFinishing())return;showState(null);setBusy(false);
                    new AlertDialog.Builder(this).setTitle(tr("预设操作未获确认")).setMessage(tr("将重新读取播放器状态；尚未初始化的预设需要先保存。"))
                        .setPositiveButton(tr("确定"),(dialog,which)->reload()).show();});
            }
        });
    }
    private void apply(String key, String value) {
        if (busy || !SonyAudioStatePolicy.editable(state, key)) return;
        Map<String, String> changes = SonyAudioControlPolicy.changes(state, key, value);
        if ("1".equals(state.get("sony_readonly"))) { applyExternal(changes);return; }
        final String acceptedValue = changes.get(key);
        String revision = state.get("sony_revision");setBusy(true);status.setSummary(tr("正在应用并保存…"));
        worker.execute(() -> {
            try {
                StringBuilder request = new StringBuilder("sony_revision=").append(revision);
                for (Map.Entry<String, String> change : changes.entrySet()) request.append(';').append(change.getKey()).append('=').append(change.getValue());
                int result = (Integer) Class.forName("android.media.AudioSystem").getMethod("setParameters", String.class)
                        .invoke(null, request.toString());
                if(result!=0)throw new IllegalStateException(tr("设置请求失败（")+result+"）");
                Map<String,String> observed;try{observed=readState();}catch(Exception ignored){observed=null;}
                final Map<String,String> fresh=observed;
                runOnUiThread(() -> {if (isFinishing()) return;showState(fresh);status.setSummary(acceptedValue.equals(value) ? tr("设置请求已发送") : tr("已发送 AVLS 限制后的音量请求"));setBusy(false);});
            } catch (Exception e) {
                runOnUiThread(() -> {if (isFinishing()) return;showState(null);setBusy(false);new AlertDialog.Builder(this).setTitle(tr("未能应用设置"))
                    .setMessage(tr("本次设置未获确认，将重新读取播放器状态。")).setPositiveButton(tr("确定"), (dialog, which) -> reload()).show();});
            }
        });
    }
    private void applyExternal(Map<String, String> changes) {
        String generation = state.get("sony_owner_generation"), revision = state.get("sony_revision");
        String id = java.util.UUID.randomUUID().toString().replace("-", "");
        String request = SonyAudioControlPolicy.request(generation, revision, id, changes);
        setBusy(true);status.setSummary(tr("正在应用并保存…"));
        worker.execute(() -> {
            try {
                Class<?> system = Class.forName("android.media.AudioSystem");
                int result = (Integer) system.getMethod("setParameters", String.class).invoke(null, request);
                if (result != 0) throw new IllegalStateException(tr("未接受设置请求"));
                Map<String, String> fresh;
                try { fresh = readState(); } catch (Exception unavailable) { fresh = null; }
                final Map<String, String> current = fresh;
                runOnUiThread(() -> {if (isFinishing()) return;showState(current);setBusy(false);
                    status.setSummary(tr("设置请求已发送；当前输出状态可重新读取"));});
            } catch (Exception e) {
                runOnUiThread(() -> {if (isFinishing()) return;showState(null);setBusy(false);
                    new AlertDialog.Builder(this).setTitle(tr("设置未获确认")).setMessage(tr("将重新读取播放器状态。本次请求不会自动重发。"))
                        .setPositiveButton(tr("确定"), (dialog, which) -> reload()).show();});
            }
        });
    }
}
