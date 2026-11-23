// The async clear can move the state back after the set path raises it.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-013 WARNING
module top(input logic clka, clkb, rst_n, set_i, output logic seen_o);
  logic alarm_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) alarm_q <= 1'b0;
    else alarm_q <= alarm_q | set_i;
  always_ff @(posedge clkb) begin s1 <= alarm_q; s2 <= s1; end
  assign seen_o = s2;
endmodule
