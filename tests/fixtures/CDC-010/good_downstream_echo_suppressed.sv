// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A downstream same-domain register copying an already reconvergent register without new asynchronous influences is suppressed.
// expect-count: CDC-010 WARNING 1
module top(input logic clka, clkb, rst_n, input logic [1:0] d, output logic y);
  logic a_q, b_q, s1a, s2a, s1b, s2b, r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {b_q, a_q} <= '0; else {b_q, a_q} <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin
    {s2a, s1a, s2b, s1b} <= '0;
  end else begin
    {s2a, s1a} <= {s1a, a_q};
    {s2b, s1b} <= {s1b, b_q};
  end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r1 <= 1'b0; else r1 <= s2a & s2b;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r2 <= 1'b0; else r2 <= r1;
  assign y = r2;
endmodule
