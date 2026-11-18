// Fixed-priority arbiter over four requesters with a registered one-hot grant.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] req,
  output logic [3:0] grant
);
  logic [3:0] hold_mask;
  logic [3:0] eff_req;
  logic [3:0] grant_n;
  logic [3:0] grant_q;

  assign eff_req = req & ~hold_mask;

  always_comb begin
    grant_n = 4'b0000;
    if      (eff_req[0]) grant_n = 4'b0001;
    else if (eff_req[1]) grant_n = 4'b0010;
    else if (eff_req[2]) grant_n = 4'b0100;
    else if (eff_req[3]) grant_n = 4'b1000;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) grant_q <= 4'b0000;
    else        grant_q <= grant_n;
  end

  assign grant = grant_q;
endmodule
