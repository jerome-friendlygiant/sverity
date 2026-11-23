// Single-clock Gray-coded event counter compared against a local count.
module gray_ptr_gen (
  input  logic       clk,
  input  logic       rst_ni,
  input  logic       inc_i,
  output logic [3:0] gray_o
);
  logic [3:0] bin_q;
  logic [3:0] gray_q;

  always_ff @(posedge clk) begin
    if (!rst_ni)     bin_q <= 4'd0;
    else if (inc_i)  bin_q <= bin_q + 4'd1;
  end

  always_ff @(posedge clk) begin
    if (!rst_ni) gray_q <= 4'd0;
    else         gray_q <= bin_q ^ (bin_q >> 1);
  end

  assign gray_o = gray_q;
endmodule

module top (
  input  logic clk_sys,
  input  logic rst_ni,
  input  logic inc_i,
  input  logic ref_inc_i,
  output logic match_o
);
  logic [3:0] gray_w;
  logic [3:0] pipe1_q;
  logic [3:0] pipe2_q;
  logic [3:0] decoded;
  logic [3:0] ref_q;
  logic       match_q;

  gray_ptr_gen u_gen (
    .clk    (clk_sys),
    .rst_ni (rst_ni),
    .inc_i  (inc_i),
    .gray_o (gray_w)
  );

  always_ff @(posedge clk_sys) begin
    if (!rst_ni) begin
      pipe1_q <= 4'd0;
      pipe2_q <= 4'd0;
    end else begin
      pipe1_q <= gray_w;
      pipe2_q <= pipe1_q;
    end
  end

  assign decoded = {pipe2_q[3],
                    pipe2_q[3] ^ pipe2_q[2],
                    pipe2_q[3] ^ pipe2_q[2] ^ pipe2_q[1],
                    pipe2_q[3] ^ pipe2_q[2] ^ pipe2_q[1] ^ pipe2_q[0]};

  always_ff @(posedge clk_sys) begin
    if (!rst_ni)        ref_q <= 4'd0;
    else if (ref_inc_i) ref_q <= ref_q + 4'd1;
  end

  always_ff @(posedge clk_sys) begin
    if (!rst_ni) match_q <= 1'b0;
    else         match_q <= (decoded == ref_q);
  end

  assign match_o = match_q;
endmodule
