// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk);
  logic q;
  task automatic get(ref logic v); logic t; t = 1; endtask
  always @(posedge clk) q = 1;
  always @* get(q);
endmodule
