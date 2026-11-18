// sverity-args: --top top
// A 3-bit index into a table of six entries: whether the design keeps the index in range is an input contract, stated as an obligation.
// expect-unresolved: LNT-006 6
module top(input logic [2:0] address, output logic [7:0] value);
  localparam logic [5:0][7:0] TABLE = {8'd11, 8'd9, 8'd7, 8'd5, 8'd3, 8'd1};
  assign value = TABLE[address];
endmodule
