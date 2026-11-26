// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 2-stage reset synchroniser written with synthesis-ignored NBA intra-assignment delays (<= #TCQ) is recognised: RDC-001 INFO (synchroniser) and RDC-003 INFO, no WARNING.
// expect: RDC-001 INFO
// expect-none: RDC-001 WARNING
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
module top(input logic clka, por_n, d, output logic q);
  localparam TCQ = 1;
  logic [1:0] sr;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) sr <= #TCQ 2'b00;
    else sr <= #TCQ {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q <= #TCQ 1'b0;
    else q <= #TCQ d;
endmodule
