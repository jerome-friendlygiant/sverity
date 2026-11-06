// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module lane(input logic [4:0] d_i, output logic [4:0] q_o);
  assign q_o = d_i;
endmodule
module top(input logic [9:0] d_i, output logic [9:0] q_o);
  lane u_lo(.d_i(d_i[4:0]),.q_o(q_o[4:0]));
  lane u_hi(.d_i(d_i[9:5]),.q_o(q_o[9:5]));
endmodule
