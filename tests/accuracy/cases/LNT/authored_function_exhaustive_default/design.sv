// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [2:0] code_i, output logic [7:0] mask_o);
  function automatic logic [7:0] decode(input logic [2:0] code);
    logic [7:0] value;
    value = 8'b0;
    case (code)
      3'd0: value = 8'h01;
      3'd1: value = 8'h03;
      3'd2: value = 8'h07;
      default: value = 8'hff;
    endcase
    return value;
  endfunction
  assign mask_o = decode(code_i);
endmodule
