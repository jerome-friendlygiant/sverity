// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A wait() timing control before a constant write keeps CDC-015 one-way handshake scrutiny active.
// expect: CDC-015 WARNING
module top(input logic clka, clkb, rst_a, en, output logic q);
  logic w, req_q, s1, s2, ack_q, a1, a2;
  always @(posedge clka) begin
    wait (1'b0);
    w <= 1'b0;
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) req_q <= 1'b0;
    else if (en) req_q <= w;
    else req_q <= 1'b0;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) begin
      {s2, s1} <= '0;
      ack_q <= 1'b0;
    end else begin
      {s2, s1} <= {s1, req_q};
      ack_q <= s2;
    end
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) {a2, a1} <= '0;
    else {a2, a1} <= {a1, ack_q};
  end
  assign q = a2 ^ s2;
endmodule
