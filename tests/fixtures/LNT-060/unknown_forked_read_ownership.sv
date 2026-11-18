// sverity-args: --top top
// expect: LNT-060 WARNING
// expect-unresolved: LNT-060
module top(input logic clk, output logic y);
  logic q;
  task automatic launch;
    fork y = q; join_none
  endtask
  always @(posedge clk) begin q = 1; launch(); end
endmodule
