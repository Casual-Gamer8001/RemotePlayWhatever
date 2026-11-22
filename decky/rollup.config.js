import deckyPlugin from "@decky/rollup";

export default deckyPlugin({
  output: {
    // dist/index.js bundles qrcode-generator, which asks for its notice to be kept.
    banner:
      "/*! Includes qrcode-generator (https://github.com/kazuhikoarase/qrcode-generator)," +
      " Copyright (c) 2009 Kazuhiko Arase, MIT License. */",
  },
});
