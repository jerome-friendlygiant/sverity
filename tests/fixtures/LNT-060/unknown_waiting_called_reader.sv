// sverity-args: --top top
// expect: LNT-060 WARNING 10
// expect-unresolved: LNT-060 10
module top(input logic clk, d, output logic y);
  logic q;
  task automatic read_later;
    wait (q);
    y = q;
  endtask
  always_comb read_later();
  always @(posedge clk) q = d;
endmodule
