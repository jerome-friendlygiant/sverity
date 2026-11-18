// sverity-args: --top top
// A reset loop (for and foreach) over every element, and per-element genvar processes: each reset element has a load.
// expect-none: LNT-020
// expect-none: LNT-067
module top #(parameter int N = 4)(input logic clk, rst_ni, we, input logic [1:0] wa, input logic [7:0] d [N], input logic [7:0] wd, output logic [7:0] y);
  logic [7:0] state_q [N];
  logic [7:0] regfile [N];
  logic [7:0] fifo [N];
  for (genvar s = 0; s < N; s++) begin : g_state
    always_ff @(posedge clk or negedge rst_ni) begin
      if (!rst_ni) state_q[s] <= '0;
      else state_q[s] <= d[s];
    end
  end
  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int i = 0; i < N; i++) regfile[i] <= '0;
    end else if (we) regfile[wa] <= wd;
  end
  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) begin
      foreach (fifo[i]) fifo[i] <= '0;
    end else fifo[wa] <= wd;
  end
  assign y = state_q[0] ^ regfile[wa] ^ fifo[wa];
endmodule
