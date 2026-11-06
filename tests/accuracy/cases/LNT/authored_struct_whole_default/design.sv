// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic accept_i, input logic [7:0] data_i, output logic [9:0] packed_o);
  typedef struct packed {logic valid; logic error; logic [7:0] data;} response_t;
  response_t response;
  always_comb begin
    response = '0;
    if (accept_i) begin
      response.valid = 1'b1;
      response.data = data_i;
    end
  end
  assign packed_o = response;
endmodule
