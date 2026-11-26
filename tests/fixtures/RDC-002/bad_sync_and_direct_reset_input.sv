// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A top-level reset input that feeds a reset synchroniser AND also directly resets an ordinary functional register must still report RDC-002.
// expect: RDC-002 WARNING
// expect-unresolved: RDC-002
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, por_n, d, output logic q_sync, q_raw);
  logic [1:0] sr;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) sr <= 2'b00;
    else sr <= {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q_sync <= 1'b0;
    else q_sync <= d;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) q_raw <= 1'b0;
    else q_raw <= d;
endmodule
