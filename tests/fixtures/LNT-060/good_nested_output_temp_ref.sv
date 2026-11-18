// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk);
  logic q;
  task automatic inner(ref logic v); logic t; t = v; endtask
  task automatic outer(output logic v); inner(v); endtask
  always @(posedge clk) q = 1;
  always @* outer(q);
endmodule
