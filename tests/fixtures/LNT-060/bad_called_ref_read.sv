// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q;
  task automatic get(ref logic v); y = v; endtask
  always @(posedge clk) q = 1;
  always @* get(q);
endmodule
