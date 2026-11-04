// sverity-args: --top top --sdc @FIXTURES@/CDC-003/excl.sdc
// expect: CDC-037 WARNING 8
// Clocks declared exclusive, yet a synchroniser sits between them: the constraint and the structure disagree.
module top(input logic clka, clkb, d, output logic q);
  logic launch, stage1;
  always_ff @(posedge clka) launch <= d;
  // Destination chain of depth 2.
  always_ff @(posedge clkb) begin
    stage1 <= launch;
    q <= stage1;
  end
endmodule
