using System;
using System.IO;
using System.Threading.Tasks;
using Windows.Storage;

namespace SteamControllerRemapperWidget
{
    internal sealed class BridgeResponse
    {
        public bool IsOk { get; set; }
        public string Payload { get; set; }
        public string Error { get; set; }
    }

    internal static class IpcClient
    {
        private const string StateFileName = "widget-state.json";
        private const string RequestFileName = "widget-request.txt";
        private const string ResponseFileName = "widget-response.txt";

        public static async Task<string> ReadStateAsync()
        {
            StorageFolder folder = ApplicationData.Current.LocalFolder;
            try
            {
                StorageFile file = await folder.GetFileAsync(StateFileName);
                return await FileIO.ReadTextAsync(file);
            }
            catch
            {
                return null;
            }
        }

        public static async Task<BridgeResponse> SendAsync(string command, string payload = "")
        {
            StorageFolder folder = ApplicationData.Current.LocalFolder;
            string requestId = Guid.NewGuid().ToString("N");
            string requestText = requestId + "\n" + command + "\n" + (payload ?? string.Empty) + "\n";

            StorageFile requestFile = await folder.CreateFileAsync(RequestFileName, CreationCollisionOption.ReplaceExisting);
            await FileIO.WriteTextAsync(requestFile, requestText);

            for (int attempt = 0; attempt < 20; ++attempt)
            {
                await Task.Delay(100);
                try
                {
                    StorageFile responseFile = await folder.GetFileAsync(ResponseFileName);
                    string responseText = await FileIO.ReadTextAsync(responseFile);
                    string[] parts = responseText.Replace("\r", string.Empty).Split('\n');
                    if (parts.Length < 2 || !string.Equals(parts[0], requestId, StringComparison.Ordinal))
                        continue;

                    string status = parts[1];
                    string responsePayload = parts.Length >= 3 ? parts[2] : string.Empty;
                    return new BridgeResponse
                    {
                        IsOk = string.Equals(status, "OK", StringComparison.Ordinal),
                        Payload = responsePayload,
                        Error = string.Equals(status, "OK", StringComparison.Ordinal) ? null : responsePayload,
                    };
                }
                catch
                {
                }
            }

            return new BridgeResponse
            {
                IsOk = false,
                Error = "Timed out waiting for Steam Controller Remapper.",
            };
        }
    }
}
