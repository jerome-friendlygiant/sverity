// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A reset chain with combinational logic between stages is not a recognised synchroniser and must stay RDC-001 WARNING.
// expect: RDC-001 WARNING
// expect-count: RDC-001 WARNING 1
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, por_n, en, d, output logic q);
  logic s0, s1;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) begin s0 <= 1'b0; s1 <= 1'b0; end
    else begin s0 <= 1'b1; s1 <= s0 & en; end
  always_ff @(posedge clka or negedge s1)
    if (!s1) q <= 1'b0;
    else q <= d;
endmodule
