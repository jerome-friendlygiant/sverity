// sverity-args: --top top
// expect: LNT-060 WARNING
// expect-unresolved: LNT-060
module top(input logic clk, en, output logic y);
  logic q;
  task automatic put(output logic v);
    wait (en);
  endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
