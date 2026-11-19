// StockCrab in a Web Worker: messages to the worker are UCI commands, held until the engine is ready.
var stockcrabQueue = [];
var stockcrabReady = false;
self.onmessage = function (e) {
  var line = String(e.data);
  if (stockcrabReady) Module.ccall("stockcrab_command", null, ["string"], [line]);
  else stockcrabQueue.push(line);
};
