// sverity-args: --top top --sdc @DIR@/fp.sdc
// A set_false_path on a reset input that feeds both a reset synchroniser and a direct functional register must still report RDC-027.
// expect: RDC-027 WARNING
module top(input logic clka, rst_n, d, output logic q_sync, q_raw);
  logic [1:0] sr;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) sr <= 2'b00;
    else sr <= {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q_sync <= 1'b0;
    else q_sync <= d;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) q_raw <= 1'b0;
    else q_raw <= d;
endmodule
