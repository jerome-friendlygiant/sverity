// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, output logic y);
  logic q;
  task automatic read_q; y = q; endtask
  always @(posedge clk) begin
    q = 1;
    fork begin fork read_q(); join_none end join_none
  end
endmodule
