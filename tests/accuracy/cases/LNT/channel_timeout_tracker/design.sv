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
  output logic [N-1:0] timeout_o
);
  logic [N-1:0] pending_q;
  logic [7:0]   age_q [N];

  for (genvar i = 0; i < N; i++) begin : gen_chan
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        pending_q[i] <= 1'b0;
        age_q[i]     <= 8'd0;
      end else begin
        if (req_i[i])      pending_q[i] <= 1'b1;
        else if (ack_i[i]) pending_q[i] <= 1'b0;

        if (pending_q[i])  age_q[i] <= age_q[i] + 8'd1;
        else               age_q[i] <= 8'd0;
      end
    end
    assign timeout_o[i] = (age_q[i] == 8'(TIMEOUT));
  end

  // abandon requests that were never acknowledged
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      pending_q <= '0;
    end else begin
      for (int i = 0; i < N; i++) begin
        if (timeout_o[i]) pending_q[i] <= 1'b0;
      end
    end
  end

  assign pending_o = pending_q;
endmodule
