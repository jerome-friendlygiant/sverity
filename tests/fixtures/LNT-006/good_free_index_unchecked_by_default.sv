// sverity-args: --top top
// Whether a free index stays in range is an input contract: unchecked unless LNT-006.dynamic_index=true.
// expect-none: LNT-006
module top(input logic [2:0] address, output logic [7:0] value);
  localparam logic [5:0][7:0] TABLE = {8'd11, 8'd9, 8'd7, 8'd5, 8'd3, 8'd1};
  assign value = TABLE[address];
endmodule
