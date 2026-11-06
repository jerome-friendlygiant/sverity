// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module invert_leaf(input wire a_i, output wire y_o);
  assign y_o = ~a_i;
endmodule
module top(input wire enable_i, output wire active_o);
  wire bus;
  invert_leaf u_inv(.a_i(bus), .y_o(active_o));
  assign bus = enable_i & active_o;
endmodule
