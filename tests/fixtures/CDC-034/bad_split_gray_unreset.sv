// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Split non-reset constant-index Gray writes from one sequential owner under compatible control
// must still be recognized as Gray so CDC-034 catches the missing asynchronous reset.
// expect: CDC-034 WARNING
// expect-none: CDC-009
module top(input logic clka, clkb, rst_n, inc_a, inc_b, output logic [3:0] qa, qb);
  logic [3:0] ab, ag, as1, as2, bb, bg, bs1, bs2;
  // Write-side pointer: split Gray writes, NO async reset on ag!
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) ab <= '0;
    else if (inc_a && (ab != bs2)) ab <= ab + 1'b1;
  always_ff @(posedge clka) begin
    ag[3]   <= ab[3];
    ag[2:0] <= ab[2:0] ^ ab[3:1];
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin as1 <= '0; as2 <= '0; end
    else begin as1 <= ag; as2 <= as1; end
  // Read-side pointer: fully reset Gray
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin bb <= '0; bg <= '0; end
    else begin
      if (inc_b && (bb != as2)) bb <= bb + 1'b1;
      bg <= bb ^ (bb >> 1);
    end
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin bs1 <= '0; bs2 <= '0; end
    else begin bs1 <= bg; bs2 <= bs1; end
  assign qa = as2;
  assign qb = bs2;
endmodule
