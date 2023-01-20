const { Server } = require("ws");

console.log("Iniciou");

const Socket = new Server({ port: 81 });

let IS_DOOR_OPEN = false;
let IS_REGISTER_MODE = false;

Socket.on("connection", (ws) => {
  console.log("New client connected");

  ws.on("message", (message) => {
    const messageData = JSON.parse(message);
    console.log("Message received: ", messageData);

    switch (messageData.type) {
      case "getData":
        console.log("Solicitando dados.");
        const data = JSON.stringify({
          type: "getData",
          isDoorOpen: IS_DOOR_OPEN,
          isRegistrationMode: IS_REGISTER_MODE,
        });
        ws.send(data);
        break;
      case "isRegistrationMode":
        IS_REGISTER_MODE = Boolean(message.value);
        console.log("IS_REGISTER_MODE: ", IS_REGISTER_MODE);
        break;
      case "isDoorOpen":
        IS_DOOR_OPEN = Boolean(message.value);
        console.log("IS_DOOR_OPEN: ", IS_DOOR_OPEN);
        break;
    }
  });

  ws.on("close", () => console.log("Client has disconnected!"));
});

// setInterval(() => {
//   Socket.clients.forEach((client) => {
//     IS_DOOR_OPEN = !IS_DOOR_OPEN;
//     IS_REGISTER_MODE = !IS_REGISTER_MODE;
//     const data = JSON.stringify({
//       type: "getData",
//       isDoorOpen: IS_DOOR_OPEN,
//       isRegistrationMode: IS_REGISTER_MODE,
//     });
//     client.send(data);
//   });
// }, 5000);
