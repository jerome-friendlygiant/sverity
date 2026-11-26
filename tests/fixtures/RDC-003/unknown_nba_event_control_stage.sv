// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An NBA intra-assignment event control (<= @(posedge clkb)) is not a synthesis-ignored delay and must reject synchroniser recognition.
// expect: RDC-003 WARNING
// expect-unresolved: RDC-003
// expect-coverage: reset-extraction INCOMPLETE
// expect-none: RDC-003 INFO
module top(input logic clka, clkb, por_n, d, output logic q);
  logic [1:0] sr;
  always @(posedge clka or negedge por_n)
    if (!por_n) sr <= 2'b00;
    else sr <= @(posedge clkb) {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q <= 1'b0;
    else q <= d;
endmodule
