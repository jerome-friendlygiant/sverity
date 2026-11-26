// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A cross-domain reset that feeds a receiving reset synchroniser AND also directly resets an ordinary functional register must still report RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, por_n, d, output logic q_sync, q_direct);
  logic rst_a_q;
  logic [1:0] sync_b;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) rst_a_q <= 1'b0;
    else rst_a_q <= 1'b1;
  always_ff @(posedge clkb or negedge rst_a_q)
    if (!rst_a_q) sync_b <= 2'b00;
    else sync_b <= {sync_b[0], 1'b1};
  always_ff @(posedge clkb or negedge sync_b[1])
    if (!sync_b[1]) q_sync <= 1'b0;
    else q_sync <= d;
  always_ff @(posedge clkb or negedge rst_a_q)
    if (!rst_a_q) q_direct <= 1'b0;
    else q_direct <= d;
endmodule
