// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk);
  logic q;
  task automatic get(output logic v); logic t; t = v; endtask
  always @(posedge clk) q = 1;
  always @* get(q);
endmodule
