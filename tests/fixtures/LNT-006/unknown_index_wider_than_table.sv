// sverity-args: --top top --option LNT-006.dynamic_index=true
// A 3-bit index can address entries 6 and 7 of a six-entry table; whether the inputs ever do is not in the RTL.
// expect: LNT-006 WARNING 6
module top(input logic [2:0] address, output logic [7:0] value);
  localparam logic [5:0][7:0] TABLE = {8'd11, 8'd9, 8'd7, 8'd5, 8'd3, 8'd1};
  assign value = TABLE[address];
endmodule
