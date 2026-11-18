// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top #(
  parameter int N       = 4,
  parameter int TIMEOUT = 200
) (
  input  logic         clk_i,
  input  logic         rst_ni,
  input  logic [N-1:0] req_i,
  input  logic [N-1:0] ack_i,
  output logic [N-1:0] pending_o,
  output logic [N-1:0] timed_out_o
);
  logic [2*N-1:0] flags_q;
  logic [7:0]     age_q [N];

  // low half: pending request per lane
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      flags_q[N-1:0] <= '0;
    end else begin
      for (int i = 0; i < N; i++) begin
        if (req_i[i])      flags_q[i] <= 1'b1;
        else if (ack_i[i]) flags_q[i] <= 1'b0;
      end
    end
  end

  // high half: sticky timeout per lane
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      flags_q[2*N-1:N] <= '0;
      for (int i = 0; i < N; i++) age_q[i] <= 8'd0;
    end else begin
      for (int i = 0; i < N; i++) begin
        age_q[i] <= flags_q[i] ? age_q[i] + 8'd1 : 8'd0;
        if (age_q[i] == 8'(TIMEOUT)) flags_q[N+i] <= 1'b1;
        else if (ack_i[i])           flags_q[N+i] <= 1'b0;
      end
    end
  end

  assign pending_o   = flags_q[N-1:0];
  assign timed_out_o = flags_q[2*N-1:N];
endmodule
