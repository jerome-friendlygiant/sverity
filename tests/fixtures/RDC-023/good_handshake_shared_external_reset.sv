// sverity-args: --top top --sdc @DIR@/handshake.sdc
// A toggle handshake whose two endpoints and all synchroniser stages are reset to one idle value from one external reset, through two
// reset synchronisers: the release order stays undefined, but the handshake is idle in reset.
// expect: RDC-023 INFO
// expect-none: RDC-023 WARNING
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_ni,
  input  logic issue_i,
  output logic busy_a_o,
  output logic done_b_o
);
  logic       rst_a_n, rst_b_n, ra1_q, rb1_q;
  logic       req_a_q, ack_a1_q, ack_a_q, busy_q;
  logic       req_b1_q, req_b_q, ack_b_q, done_b_q;

  always_ff @(posedge clk_a or negedge rst_ni)
    if (!rst_ni) {rst_a_n, ra1_q} <= 2'b00;
    else         {rst_a_n, ra1_q} <= {ra1_q, 1'b1};

  always_ff @(posedge clk_b or negedge rst_ni)
    if (!rst_ni) {rst_b_n, rb1_q} <= 2'b00;
    else         {rst_b_n, rb1_q} <= {rb1_q, 1'b1};

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      req_a_q  <= 1'b0;
      ack_a1_q <= 1'b0;
      ack_a_q  <= 1'b0;
      busy_q   <= 1'b0;
    end else begin
      ack_a1_q <= ack_b_q;
      ack_a_q  <= ack_a1_q;
      if (!busy_q && issue_i) begin
        req_a_q <= ~req_a_q;
        busy_q  <= 1'b1;
      end else if (busy_q && (ack_a_q == req_a_q)) begin
        busy_q  <= 1'b0;
      end
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      req_b1_q <= 1'b0;
      req_b_q  <= 1'b0;
      ack_b_q  <= 1'b0;
      done_b_q <= 1'b0;
    end else begin
      req_b1_q <= req_a_q;
      req_b_q  <= req_b1_q;
      if (req_b_q != ack_b_q) begin
        ack_b_q  <= req_b_q;
        done_b_q <= ~done_b_q;
      end
    end

  assign busy_a_o = busy_q;
  assign done_b_o = done_b_q;

endmodule
