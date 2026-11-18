// Round-robin arbiter with a rotating priority pointer. The pointer moves to
// the slot after the most recent winner so that every requester is served.
module top #(
  parameter int N = 6
) (
  input  logic         clk,
  input  logic         rst_n,
  input  logic [N-1:0] req,
  output logic [N-1:0] grant,
  output logic         any_grant
);
  localparam int PW = $clog2(N);

  logic [PW-1:0] ptr;
  logic [PW-1:0] sel;
  logic          found;

  always_comb begin
    grant = '0;
    found = 1'b0;
    sel   = '0;
    for (int k = 0; k < N; k++) begin
      int idx;
      idx = (int'(ptr) + k) % N;
      if (!found && req[idx]) begin
        found      = 1'b1;
        grant[idx] = 1'b1;
        sel        = PW'(idx);
      end
    end
  end

  assign any_grant = found;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)     ptr <= '0;
    else if (found) ptr <= (sel == PW'(N - 1)) ? '0 : sel + 1'b1;
  end
endmodule
