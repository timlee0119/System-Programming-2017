const fs = require("fs");

let count = 0;

module.exports = {
    quit: function() {
        return JSON.stringify({
            cmd: "quit",
        }) + "\n";
    },
    try_match: function() {
        const info = JSON.parse(fs.readFileSync("./info.json").toString());
        info.cmd = "try_match";
        return JSON.stringify(info) + "\n";
    },
    send_message: function(msg) {
        count += 1;
        return JSON.stringify({
            cmd: "send_message",
            sequence: count,
            message: msg,
        }) + "\n";
    }
}