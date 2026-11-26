// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 1-stage reset chain (< RDC-006.min_stages) is not a full recognised synchroniser and must stay RDC-001 WARNING.
// expect: RDC-001 WARNING
// expect-count: RDC-001 WARNING 1
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, por_n, d, output logic q);
  logic sr;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) sr <= 1'b0;
    else sr <= 1'b1;
  always_ff @(posedge clka or negedge sr)
    if (!sr) q <= 1'b0;
    else q <= d;
endmodule
