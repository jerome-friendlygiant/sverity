// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One launch bit is copied into two synchronizer chains; repeated provenance is not two independent influences.
// expect-none: CDC-010
module top(input logic clka, clkb, d, output logic q);
  logic launch, a1, a2, b1, b2;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) begin a1 <= launch; a2 <= a1; b1 <= launch; b2 <= b1; end
  always_ff @(posedge clkb) q <= a2 ^ b2;
endmodule
