// sverity-args: --top top
// expect-confidence: LNT-060 established
module top(input logic clk, en, output logic y);
  logic q;
  task automatic put(output logic v); fork wait(en); join_none endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
