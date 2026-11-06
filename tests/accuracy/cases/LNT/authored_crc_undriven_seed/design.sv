// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] data_i, output logic [7:0] crc_o);
  logic [7:0] seed;
  function automatic logic [7:0] update_crc(input logic [7:0] d, s);
    return {s[6:0],s[7]} ^ d;
  endfunction
  assign crc_o = update_crc(data_i, seed);
endmodule
