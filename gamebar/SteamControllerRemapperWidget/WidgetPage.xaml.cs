using Microsoft.Gaming.XboxGameBar;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Windows.Data.Json;
using Windows.Storage;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;

namespace SteamControllerRemapperWidget
{
    public sealed partial class WidgetPage : Page
    {
        private sealed class MappingOption
        {
            public string Token { get; set; }
            public string Label { get; set; }
            public bool IsCustomPlaceholder { get; set; }
            public override string ToString() => Label;
        }

        private sealed class BindingView
        {
            public string PaddleId { get; set; }
            public ComboBox ComboBox { get; set; }
        }

        private static readonly MappingOption[] BaseMappingOptions =
        {
            new MappingOption { Token = "None", Label = "Unmapped" },
            new MappingOption { Token = "A", Label = "A / Cross" },
            new MappingOption { Token = "B", Label = "B / Circle" },
            new MappingOption { Token = "X", Label = "X / Square" },
            new MappingOption { Token = "Y", Label = "Y / Triangle" },
            new MappingOption { Token = "LeftShoulder", Label = "Left Shoulder" },
            new MappingOption { Token = "RightShoulder", Label = "Right Shoulder" },
            new MappingOption { Token = "View", Label = "View / Share" },
            new MappingOption { Token = "Menu", Label = "Menu / Options" },
            new MappingOption { Token = "LeftThumb", Label = "Left Stick Click" },
            new MappingOption { Token = "RightThumb", Label = "Right Stick Click" },
            new MappingOption { Token = "Guide", Label = "Guide / PS" },
            new MappingOption { Token = "DPadUp", Label = "D-Pad Up" },
            new MappingOption { Token = "DPadRight", Label = "D-Pad Right" },
            new MappingOption { Token = "DPadDown", Label = "D-Pad Down" },
            new MappingOption { Token = "DPadLeft", Label = "D-Pad Left" },
        };

        private XboxGameBarWidget widget;
        private readonly DispatcherTimer refreshTimer = new DispatcherTimer();
        private readonly List<BindingView> bindingViews = new List<BindingView>();
        private bool refreshInProgress;
        private bool suppressControlEvents;
        private int openDropDownCount;
        private string lastStateJson;
        private string activeProfileId = "default";

        private async Task AppendDebugLogAsync(string message)
        {
            try
            {
                StorageFile file = await ApplicationData.Current.LocalFolder.CreateFileAsync(
                    "widget-debug.log", CreationCollisionOption.OpenIfExists);
                string line = DateTimeOffset.Now.ToString("O") + " " + message + Environment.NewLine;
                await FileIO.AppendTextAsync(file, line);
            }
            catch
            {
            }
        }

        public WidgetPage()
        {
            InitializeComponent();
            bindingViews.Add(new BindingView { PaddleId = "L4", ComboBox = L4ComboBox });
            bindingViews.Add(new BindingView { PaddleId = "L5", ComboBox = L5ComboBox });
            bindingViews.Add(new BindingView { PaddleId = "R4", ComboBox = R4ComboBox });
            bindingViews.Add(new BindingView { PaddleId = "R5", ComboBox = R5ComboBox });
            bindingViews.Add(new BindingView { PaddleId = "QAM", ComboBox = QamComboBox });

            foreach (BindingView binding in bindingViews)
            {
                binding.ComboBox.Tag = binding.PaddleId;
            }

            refreshTimer.Interval = TimeSpan.FromSeconds(1);
            refreshTimer.Tick += RefreshTimer_Tick;
        }

        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);
            widget = e.Parameter as XboxGameBarWidget;
            refreshTimer.Start();
            try
            {
                await RefreshAsync(forceUiUpdate: true);
            }
            catch (Exception ex)
            {
                StatusTextBlock.Text = "Widget refresh failed: " + ex.Message;
            }
        }

        protected override void OnNavigatedFrom(NavigationEventArgs e)
        {
            refreshTimer.Stop();
            base.OnNavigatedFrom(e);
        }

        private async void RefreshTimer_Tick(object sender, object e)
        {
            await RefreshAsync();
        }

        private async void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            await RefreshAsync(forceUiUpdate: true);
        }

        private async void RefreshLibraryButton_Click(object sender, RoutedEventArgs e)
        {
            StatusTextBlock.Text = "Refreshing library...";
            BridgeResponse response = await IpcClient.SendAsync("REFRESH_LIBRARY");
            if (!response.IsOk)
            {
                StatusTextBlock.Text = "Library refresh failed: " + response.Error;
                return;
            }

            await RefreshAsync("Library refreshed.", true);
        }

        private async void OpenDesktopEditorButton_Click(object sender, RoutedEventArgs e)
        {
            StatusTextBlock.Text = "Opening desktop editor...";
            await AppendDebugLogAsync("OpenDesktopEditor clicked");
            BridgeResponse response = await IpcClient.SendAsync("OPEN_DESKTOP_EDITOR");
            if (!response.IsOk)
            {
                await AppendDebugLogAsync("OpenDesktopEditor failed: " + response.Error);
                StatusTextBlock.Text = "Desktop editor failed: " + response.Error;
                return;
            }

            StatusTextBlock.Text = "Desktop editor opened.";
            await AppendDebugLogAsync("OpenDesktopEditor response ok");
            if (widget != null)
            {
                await AppendDebugLogAsync("OpenDesktopEditor waiting before minimize");
                await Task.Delay(250);
                await AppendDebugLogAsync("OpenDesktopEditor closing widget");
                var widgetControl = new XboxGameBarWidgetControl(widget);
                await widgetControl.CloseAsync("ProfileWidget");
                await AppendDebugLogAsync("OpenDesktopEditor close returned");
            }
        }

        private async void ProfilesComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (suppressControlEvents || !(ProfilesComboBox.SelectedItem is string selected) || string.IsNullOrWhiteSpace(selected))
                return;

            StatusTextBlock.Text = "Applying profile...";
            BridgeResponse response = await IpcClient.SendAsync("APPLY_PROFILE", selected);
            if (!response.IsOk)
            {
                StatusTextBlock.Text = "Profile switch failed: " + response.Error;
                return;
            }

            await RefreshAsync("Profile applied.", true);
        }

        private async void BindingComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (suppressControlEvents || !(sender is ComboBox comboBox) || !(comboBox.SelectedItem is MappingOption option))
                return;

            if (option.IsCustomPlaceholder)
                return;

            string paddleId = comboBox.Tag as string;
            if (string.IsNullOrWhiteSpace(paddleId) || string.IsNullOrWhiteSpace(activeProfileId))
                return;

            StatusTextBlock.Text = "Saving " + paddleId + "...";
            string payload = activeProfileId + "\t" + paddleId + "\t" + option.Token;
            BridgeResponse response = await IpcClient.SendAsync("SET_PROFILE_GAMEPAD_BINDING", payload);
            if (!response.IsOk)
            {
                StatusTextBlock.Text = "Binding update failed: " + response.Error;
                return;
            }

            await RefreshAsync(paddleId + " updated.", true);
        }

        private async void AutoSwitchCheckBox_Click(object sender, RoutedEventArgs e)
        {
            if (suppressControlEvents)
                return;

            string value = AutoSwitchCheckBox.IsChecked == true ? "1" : "0";
            BridgeResponse response = await IpcClient.SendAsync("SET_AUTO_SWITCH", value);
            if (!response.IsOk)
            {
                StatusTextBlock.Text = "Auto-switch update failed: " + response.Error;
                return;
            }

            StatusTextBlock.Text = AutoSwitchCheckBox.IsChecked == true
                ? "Auto-switch enabled."
                : "Auto-switch disabled.";
        }

        private void ComboBox_DropDownOpened(object sender, object e)
        {
            openDropDownCount++;
        }

        private void ComboBox_DropDownClosed(object sender, object e)
        {
            if (openDropDownCount > 0)
                openDropDownCount--;
        }

        private async Task RefreshAsync(string successStatus = null, bool forceUiUpdate = false)
        {
            if (refreshInProgress)
                return;

            refreshInProgress = true;
            if (!string.IsNullOrEmpty(successStatus))
                StatusTextBlock.Text = successStatus;

            try
            {
                string stateResponse = await IpcClient.ReadStateAsync();
                if (!TryParseObject(stateResponse, out JsonObject stateObject))
                {
                    StatusTextBlock.Text = "Could not reach Steam Controller Remapper. Make sure the desktop app is running.";
                    return;
                }

                bool stateChanged = !string.Equals(stateResponse, lastStateJson, StringComparison.Ordinal);
                if (!forceUiUpdate && !stateChanged)
                {
                    if (string.IsNullOrEmpty(successStatus))
                        StatusTextBlock.Text = "Connected.";
                    return;
                }

                if (!forceUiUpdate && openDropDownCount > 0)
                {
                    if (string.IsNullOrEmpty(successStatus))
                        StatusTextBlock.Text = "Connected.";
                    return;
                }

                lastStateJson = stateResponse;
                BindState(stateObject);
                StatusTextBlock.Text = successStatus ?? "Connected.";
            }
            catch (Exception ex)
            {
                StatusTextBlock.Text = "Widget refresh failed: " + ex.Message;
            }
            finally
            {
                refreshInProgress = false;
            }
        }

        private void BindState(JsonObject stateObject)
        {
            suppressControlEvents = true;
            try
            {
                JsonArray gamesArray = stateObject.ContainsKey("installedGames")
                    ? stateObject.GetNamedArray("installedGames")
                    : new JsonArray();
                activeProfileId = stateObject.GetNamedString("activeProfileId", "default");
                string detectedProfileId = stateObject.GetNamedString("detectedProfileId", string.Empty);
                bool autoSwitch = stateObject.GetNamedBoolean("autoSwitchProfiles", false);

                ProfilesComboBox.Items.Clear();
                ProfilesComboBox.Items.Add("default");
                foreach (string game in gamesArray.Select(value => value.GetString()).Where(name => !string.Equals(name, "default", StringComparison.OrdinalIgnoreCase)))
                    ProfilesComboBox.Items.Add(game);
                ProfilesComboBox.SelectedItem = ProfilesComboBox.Items.Cast<object>()
                    .Select(item => item as string)
                    .FirstOrDefault(item => string.Equals(item, activeProfileId, StringComparison.OrdinalIgnoreCase)) ?? "default";

                ActiveProfileTextBlock.Text = "Active profile: " + DisplayProfile(activeProfileId);
                DetectedGameTextBlock.Text = string.IsNullOrEmpty(detectedProfileId)
                    ? "Detected game: none"
                    : "Detected game: " + DisplayProfile(detectedProfileId);
                AutoSwitchCheckBox.IsChecked = autoSwitch;

                JsonObject profileObject = stateObject.ContainsKey("profile")
                    ? stateObject.GetNamedObject("profile")
                    : null;
                if (profileObject != null)
                {
                    UpdateBindingEditor(profileObject, "l4", L4ComboBox);
                    UpdateBindingEditor(profileObject, "l5", L5ComboBox);
                    UpdateBindingEditor(profileObject, "r4", R4ComboBox);
                    UpdateBindingEditor(profileObject, "r5", R5ComboBox);
                    UpdateBindingEditor(profileObject, "qam", QamComboBox);
                }
            }
            finally
            {
                suppressControlEvents = false;
            }
        }

        private static void UpdateBindingEditor(JsonObject profileObject, string key, ComboBox comboBox)
        {
            comboBox.Items.Clear();
            if (!profileObject.ContainsKey(key))
            {
                foreach (MappingOption option in BaseMappingOptions)
                    comboBox.Items.Add(CloneOption(option));
                comboBox.SelectedIndex = 0;
                return;
            }

            JsonObject binding = profileObject.GetNamedObject(key);
            string display = binding.GetNamedString("display", "Unmapped");
            string actionType = binding.GetNamedString("actionType", "None");
            string mappingToken = binding.GetNamedString("mappingToken", "None");

            if (!string.Equals(actionType, "Gamepad", StringComparison.Ordinal) &&
                !string.Equals(actionType, "UseMenuMapping", StringComparison.Ordinal) &&
                !string.Equals(actionType, "None", StringComparison.Ordinal))
            {
                comboBox.Items.Add(new MappingOption
                {
                    Token = mappingToken,
                    Label = "Current custom: " + display,
                    IsCustomPlaceholder = true,
                });
            }

            foreach (MappingOption option in BaseMappingOptions)
                comboBox.Items.Add(CloneOption(option));

            MappingOption selected = comboBox.Items.Cast<MappingOption>()
                .FirstOrDefault(item => item.IsCustomPlaceholder);
            if (selected == null)
            {
                selected = comboBox.Items.Cast<MappingOption>()
                    .FirstOrDefault(item => string.Equals(item.Token, mappingToken, StringComparison.OrdinalIgnoreCase)
                                         && !item.IsCustomPlaceholder);
            }
            if (selected == null && comboBox.Items.Count > 0)
                selected = comboBox.Items[0] as MappingOption;
            comboBox.SelectedItem = selected;
        }

        private static MappingOption CloneOption(MappingOption option)
        {
            return new MappingOption
            {
                Token = option.Token,
                Label = option.Label,
                IsCustomPlaceholder = option.IsCustomPlaceholder,
            };
        }

        private static bool TryParseObject(string response, out JsonObject jsonObject)
        {
            jsonObject = null;
            if (string.IsNullOrWhiteSpace(response))
                return false;
            return JsonObject.TryParse(response, out jsonObject);
        }

        private static string DisplayProfile(string profileId)
        {
            return string.Equals(profileId, "default", StringComparison.OrdinalIgnoreCase)
                ? "Default"
                : profileId;
        }
    }
}
