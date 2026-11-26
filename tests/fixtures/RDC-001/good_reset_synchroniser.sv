// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A reset root that is the final stage of a recognised reset synchroniser fed by a top-level port is INFO (synchroniser), not WARNING.
// expect: RDC-001 INFO
// expect-none: RDC-001 WARNING
module top(input logic clka, por_n, d, output logic q);
  logic [1:0] sr;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) sr <= 2'b00;
    else sr <= {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q <= 1'b0;
    else q <= d;
endmodule
