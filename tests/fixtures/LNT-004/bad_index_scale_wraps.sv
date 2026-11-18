// sverity-args: --top top
// A constant scale whose product with an 8-bit index leaves 32 bits (255 * 2**25): the range proof fails, the finding stays.
// expect: LNT-004 WARNING 6
module top(input logic [7:0] idx, input logic [63:0] data, output logic [7:0] y);
  localparam int unsigned Scale = 32'h2000000;
  assign y = data[idx * Scale +: 8];
endmodule
