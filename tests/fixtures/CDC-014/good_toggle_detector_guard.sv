// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The event detector is an inequality branch over adjacent post-first stages.
// expect: CDC-014 INFO
// expect-none: CDC-014 WARNING
module top(input logic clka, clkb, rst_n, pulse_i, output logic pulse_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) toggle_q <= 1'b0;
    else if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= 0; s2 <= 0; s3 <= 0; pulse_o <= 0; end
    else begin
      s1 <= toggle_q; s2 <= s1; s3 <= s2;
      if (s2 != s3) pulse_o <= 1'b1;
      else pulse_o <= 1'b0;
    end
endmodule
