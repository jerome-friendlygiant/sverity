// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, output logic y);
  logic q;
  task automatic put(output logic v); v = 1; endtask
  always @(posedge clk) begin
    fork put(q); join_none
    y <= q;
  end
endmodule
