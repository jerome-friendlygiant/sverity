// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] payload_i, output logic [9:0] packed_o);
  typedef struct packed { logic valid; logic error; logic [7:0] data; } response_t;
  response_t response_o;
  always_comb begin
    response_o.valid = 1'b1;
    response_o.data = payload_i;
  end
  assign packed_o = response_o;
endmodule
