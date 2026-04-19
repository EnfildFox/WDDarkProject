using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

const int Port = 12345;
var listener = new TcpListener(IPAddress.Loopback, Port);
var shutdown = new CancellationTokenSource();
var clients = new ConcurrentDictionary<string, ClientInfo>();
var nextClientNumber = 0;

Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    shutdown.Cancel();
    listener.Stop();
    Console.WriteLine("[INFO] Shutdown requested.");
};

listener.Start();
Console.WriteLine($"[INFO] Diagnostic server listening on 127.0.0.1:{Port}");

var acceptTask = Task.Run(() => AcceptLoopAsync(shutdown.Token), shutdown.Token);

try
{
    while (!shutdown.IsCancellationRequested)
    {
        string? input = Console.ReadLine();
        if (input is null) break;
        input = input.Trim();
        if (string.IsNullOrWhiteSpace(input)) continue;

        // --- НОВАЯ ОБРАБОТКА КОНСОЛЬНОГО ВВОДА ---
        if (input.StartsWith("send ", StringComparison.OrdinalIgnoreCase))
        {
            var parts = input.Split(' ', 3);
            if (parts.Length >= 3)
            {
                string targetId = parts[1];
                string command = parts[2];
                var client = clients.Values.FirstOrDefault(c => c.ClientId == targetId || c.LocalId == targetId);
                if (client != null)
                {
                    client.Commands.Enqueue(command);
                    Console.WriteLine($"[CMD] Queued for {targetId}: {command}");
                }
                else
                {
                    Console.WriteLine($"[CMD] Client {targetId} not found.");
                }
            }
            else
            {
                Console.WriteLine("[CMD] Usage: send <client_id> <command>");
            }
        }
        else
        {
            // Старая рассылка всем
            await BroadcastMessageAsync(input, shutdown.Token);
        }
    }
}
finally
{
    shutdown.Cancel();
    listener.Stop();
    try
    {
        await acceptTask;
    }
    catch (OperationCanceledException)
    {
        // Graceful shutdown.
    }
}

async Task AcceptLoopAsync(CancellationToken cancellationToken)
{
    while (!cancellationToken.IsCancellationRequested)
    {
        TcpClient tcpClient;

        try
        {
            tcpClient = await listener.AcceptTcpClientAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
            break;
        }
        catch (SocketException) when (cancellationToken.IsCancellationRequested)
        {
            break;
        }

        string localId = $"client_{Interlocked.Increment(ref nextClientNumber)}";
        var remoteEndPoint = tcpClient.Client.RemoteEndPoint?.ToString() ?? "unknown";
        var clientInfo = new ClientInfo(
            localId,
            remoteEndPoint,
            tcpClient,
            tcpClient.GetStream()
        );

        clients[localId] = clientInfo;
        Console.WriteLine($"[INFO] Client connected: {remoteEndPoint} as {localId}");
        PrintClientTable();

        _ = Task.Run(() => HandleClientAsync(clientInfo, cancellationToken), cancellationToken);
    }
}

async Task HandleClientAsync(ClientInfo clientInfo, CancellationToken cancellationToken)
{
    var remoteEndPoint = clientInfo.RemoteEndPoint;
    using var clientScope = clientInfo.Client;
    using var stream = clientInfo.Stream;
    using var reader = new StreamReader(stream, Encoding.UTF8, false, leaveOpen: true);

    try
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            string? line = await reader.ReadLineAsync(cancellationToken);
            if (line is null) break;

            clientInfo.LastSeen = DateTime.Now;

            foreach (var message in SplitLogicalMessages(line))
            {
                Console.WriteLine($"[RAW] {message}");
                await ProcessMessage(clientInfo, message);  // await async метода
                await SendLineAsync(clientInfo, "ACK");
            }
        }
    }
    catch (OperationCanceledException)
    {
        // Graceful shutdown.
    }
    catch (IOException ioException)
    {
        Console.WriteLine($"[INFO] Client disconnected ({remoteEndPoint}): {ioException.Message}");
    }
    finally
    {
        clients.TryRemove(clientInfo.LocalId, out _);
        Console.WriteLine($"[INFO] Client closed: {remoteEndPoint} ({clientInfo.LocalId})");
        PrintClientTable();
    }
}

// Изменяем на async Task
async Task ProcessMessage(ClientInfo clientInfo, string message)
{
    if (!message.StartsWith("{")) return;

    try
    {
        using JsonDocument document = JsonDocument.Parse(message);
        if (!document.RootElement.TryGetProperty("type", out JsonElement typeElement)) return;

        string? messageType = typeElement.GetString();
        if (string.Equals(messageType, "register", StringComparison.OrdinalIgnoreCase))
        {
            clientInfo.Hostname = document.RootElement.TryGetProperty("hostname", out JsonElement hostnameElement)
                ? hostnameElement.GetString() ?? "(unknown)"
                : "(unknown)";
            clientInfo.Os = document.RootElement.TryGetProperty("os", out JsonElement osElement)
                ? osElement.GetString() ?? "(unknown)"
                : "(unknown)";

            clientInfo.ClientId = clientInfo.LocalId;
            var idMessage = $"{{\"type\":\"id\",\"client_id\":\"{clientInfo.ClientId}\"}}\n";
            await SendLineAsync(clientInfo, idMessage);
            Console.WriteLine($"[INFO] Sent client_id to {clientInfo.LocalId}: {clientInfo.ClientId}");

            Console.WriteLine($"[INFO] Registered {clientInfo.LocalId}: host={clientInfo.Hostname}, os={clientInfo.Os}");
            PrintClientTable();
        }
        else if (string.Equals(messageType, "beat", StringComparison.OrdinalIgnoreCase))
        {
            PrintClientTable();
            if (clientInfo.Commands.Count > 0)
            {
                string cmd = clientInfo.Commands.Dequeue();
                await SendLineAsync(clientInfo, cmd);
                Console.WriteLine($"[CMD] Sent to {clientInfo.ClientId}: {cmd}");
            }
        }
    }
    catch (JsonException)
    {
        Console.WriteLine($"[INFO] Invalid JSON from {clientInfo.LocalId}: {message}");
    }
}

IEnumerable<string> SplitLogicalMessages(string line)
{
    if (line.StartsWith("HELLO") && line.Length > "HELLO".Length)
    {
        yield return "HELLO";
        string remainder = line.Substring("HELLO".Length);
        if (!string.IsNullOrWhiteSpace(remainder)) yield return remainder;
        yield break;
    }
    yield return line;
}

async Task SendLineAsync(ClientInfo clientInfo, string line)
{
    await clientInfo.WriteLock.WaitAsync();
    try
    {
        await clientInfo.Writer.WriteLineAsync(line);
    }
    finally
    {
        clientInfo.WriteLock.Release();
    }
}

async Task BroadcastMessageAsync(string text, CancellationToken cancellationToken)
{
    var activeClients = clients.Values.ToArray();
    if (activeClients.Length == 0)
    {
        Console.WriteLine("[INFO] No connected clients to broadcast to.");
        return;
    }

    Console.WriteLine($"[INFO] Broadcasting to {activeClients.Length} client(s): {text}");
    foreach (var clientInfo in activeClients)
    {
        if (cancellationToken.IsCancellationRequested) break;
        try
        {
            await SendLineAsync(clientInfo, text);
        }
        catch (Exception exception)
        {
            Console.WriteLine($"[INFO] Failed to send message to {clientInfo.LocalId}: {exception.Message}");
        }
    }
}

void PrintClientTable()
{
    Console.WriteLine("[CLIENTS] ID       | Hostname    | OS                    | Last Seen");
    var snapshot = clients.Values.OrderBy(c => c.LocalId, StringComparer.OrdinalIgnoreCase).ToArray();
    if (snapshot.Length == 0)
    {
        Console.WriteLine("[CLIENTS] (none)");
        return;
    }
    foreach (var client in snapshot)
    {
        Console.WriteLine(
            "[CLIENTS] " +
            $"{client.LocalId,-8} | " +
            $"{TrimForColumn(client.Hostname, 11),-11} | " +
            $"{TrimForColumn(client.Os, 21),-21} | " +
            $"{client.LastSeen:yyyy-MM-dd HH:mm:ss}"
        );
    }
}

static string TrimForColumn(string? value, int maxLength)
{
    string safeValue = string.IsNullOrWhiteSpace(value) ? "(unknown)" : value;
    if (safeValue.Length <= maxLength) return safeValue;
    return safeValue.Substring(0, maxLength - 3) + "...";
}

sealed class ClientInfo
{
    public ClientInfo(string localId, string remoteEndPoint, TcpClient client, NetworkStream stream)
    {
        LocalId = localId;
        RemoteEndPoint = remoteEndPoint;
        Client = client;
        Stream = stream;
        Writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true)
        {
            NewLine = "\n",
            AutoFlush = true
        };
        LastSeen = DateTime.Now;
        Hostname = "(unknown)";
        Os = "(unknown)";
    }

    public string LocalId { get; }
    public string RemoteEndPoint { get; }
    public TcpClient Client { get; }
    public NetworkStream Stream { get; }
    public StreamWriter Writer { get; }
    public SemaphoreSlim WriteLock { get; } = new(1, 1);
    public DateTime LastSeen { get; set; }
    public string Hostname { get; set; }
    public string Os { get; set; }
    public string ClientId { get; set; } = "";
    public Queue<string> Commands { get; } = new Queue<string>();
}