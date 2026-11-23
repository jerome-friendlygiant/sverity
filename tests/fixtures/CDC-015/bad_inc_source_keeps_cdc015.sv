// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An incrementing counter retains CDC-015 one-way handshake mismatch scrutiny.
// expect: CDC-015 WARNING
module top(input logic clka, clkb, rst_a, output logic q);
  logic [1:0] cnt;
  logic s1, s2, ack_q, a1, a2;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) cnt <= 2'b00;
    else cnt++;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) begin
      {s2, s1} <= '0;
      ack_q <= 1'b0;
    end else begin
      {s2, s1} <= {s1, cnt[0]};
      ack_q <= s2;
    end
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) {a2, a1} <= '0;
    else {a2, a1} <= {a1, ack_q};
  end
  assign q = a2 ^ s2;
endmodule
