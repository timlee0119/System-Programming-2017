const net = require("net");
const readline = require("readline");
const cmd = require("./cmd.js")

const STATUS = {
    NOT_CONNECTED: 0,
    IDLE: 1,
    MATCHING: 2,
    TALKING: 3,
};

if (process.argv.length != 4) {
    console.log("用法： node client.js [ip] [port]");
    process.exit();
}

const ip = process.argv[2];
const port = parseInt(process.argv[3]);

let status = STATUS.NOT_CONNECTED;

const client = net.createConnection({host: ip, port: port});

client.on('connect', function() {
    status = STATUS.IDLE;
    console.log(`已連上伺服器。
請輸入下一步命令
[/t （嘗試匹配）]  [/c （結束網路連線）]`);
});

function to_idle() {
    status = STATUS.IDLE;
    console.log("回到閒置狀態");
    console.log("[/t （嘗試匹配）]  [/c （結束網路連線）]");
}

let buffer = "";
client.on('data', function(data) {
    buffer += data.toString();
    let arr = buffer.split("\n");
    const len = arr.length;
    buffer = arr[len - 1];


    for (let i = 0; i < len - 1; i++) {
        let res = JSON.parse(arr[i]);
        switch (status) {
            case STATUS.NOT_CONNECTED:
                break;
            case STATUS.IDLE:
                switch(res.cmd) {
                    case "try_match":
                    status = STATUS.MATCHING;
                    console.log(`匹配中...
[/q  （放棄匹配）]  [/c （結束網路連線）]`);
                    break;
                    default:
                    console.log(`伺服器不該傳送的指令：${buffer[i]}`);
                }
                break;
            case STATUS.MATCHING:
                switch(res.cmd) {
                    case "matched":
                    status = STATUS.TALKING;
                    console.log(`成功匹配
昵稱：${res.name}
年齡：${res.age}
性別：${res.gender}
自介：${res.introduction}
[/q  （結束聊天）]  [/c  （結束網路連線）]`);
                    break;
                    case "quit":
                    to_idle();
                    break;
                    default:
                    console.log(`伺服器不該傳送的指令：${buffer[i]}`);
                }
                break;
            case STATUS.TALKING:
                switch(res.cmd) {
                    case "quit":
                    case "other_side_quit":
                    to_idle();
                    break;
                    case "receive_message":
                    case "send_message":
                    console.log(`${res.message}`);
                    break;
                    default:
                    console.log(`伺服器不該傳送的指令：${buffer[i]}`);
                }
                break;
        }
    }
});

let rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: false
});

rl.on('line', function(line) {
    switch(status) {
        case STATUS.NOT_CONNECTED:
        break;
        case STATUS.IDLE:
        switch (line) {
            case "/t":
            client.write(cmd.try_match());
            break;
            case "/c":
            client.destroy();
            process.exit();
            break;
            default:
            console.log(`[${line}] 無此指令`);
        }
        break;
        case STATUS.MATCHING:
        switch (line) {
            case "/q":
            client.write(cmd.quit());
            break;
            case "/c":
            client.destroy();
            process.exit();
            break;
            default:
            console.log(`[${line}] 無此指令`);
        }
        break;
        case STATUS.TALKING:
        switch (line) {
            case "/q":
            client.write(cmd.quit());
            break;
            case "/c":
            client.destroy();
            process.exit();
            break;
            default:
            client.write(cmd.send_message(line));
        }
        break;
    }
});