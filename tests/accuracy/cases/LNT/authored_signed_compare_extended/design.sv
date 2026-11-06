// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic signed [7:0] temperature_i, input logic [7:0] upper_i,
           output logic alarm_o);
  logic signed [8:0] upper_signed;
  assign upper_signed = $signed({1'b0,upper_i});
  assign alarm_o = $signed({temperature_i[7],temperature_i}) > upper_signed;
endmodule
