// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] payload_i, input logic permit_i, output logic [7:0] payload_o);
  assign payload_o = payload_i & {8{permit_i}};
endmodule
