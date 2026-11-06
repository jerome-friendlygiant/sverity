// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [31:0] payload_i, output logic [31:0] checksum_o);
  import "DPI-C" pure function int unsigned checksum(input int unsigned value);
  assign checksum_o = checksum(payload_i);
endmodule
