// expect-none: LNT-049
module top(input logic clk, d, i, output logic q);
  logic mem[2]; task update; mem[i] <= d; endtask
  always @(posedge clk) update(); assign q = mem[i];
endmodule
