// sverity-args: --top top --sdc @FIXTURES@/CDC-003/excl.sdc
// MUTATION of bad_exclusive_with_synchroniser.sv: the destination reads the two-stage chain, the clocks are declared exclusive.
// expect: CDC-037 WARNING
// revert: q <= stage1; => q <= launch;
// revert-expect-none: CDC-037 WARNING
module top(input logic clka, clkb, d, output logic q);
  logic launch, stage1;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) begin
    stage1 <= launch;
    q <= stage1;
  end
endmodule
