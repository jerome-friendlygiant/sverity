// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unknown: combinational logic on the second stage breaks reset synchroniser recognition (RDC-006 heuristic + RDC-003 unresolved).
// expect: RDC-006 WARNING
// expect: RDC-003 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, arst_n, en, d, output logic q);
  logic r1, r2;
  always_ff @(posedge clka or negedge arst_n)
    if (!arst_n) begin r1 <= 1'b0; r2 <= 1'b0; end
    else begin r1 <= 1'b1; r2 <= r1 & en; end
  always_ff @(posedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
endmodule
