// sverity-args: --top top
// expect: LNT-060 WARNING
// expect-confidence: LNT-060 established
module top(input logic clk, output logic y);
  logic q;
  task put(output logic v); v <= 1; endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
