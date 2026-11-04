// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
// expect-none: RDC-006
module top(input logic clka, rst_n, rst, d, output logic [7:0] q);
  logic r0_0, r0_1;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin {r0_1, r0_0} <= 2'b00; end
    else begin {r0_1, r0_0} <= {r0_0, 1'b1}; end
  always_ff @(posedge clka or negedge r0_1)
    if (!r0_1) q[0] <= 0; else q[0] <= d;
  logic r1_0, r1_1;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin r1_0 <= 1'b0; r1_1 <= 1'b0; end
    else begin r1_0 <= 1'b1; r1_1 <= r1_0; end
  always_ff @(posedge clka or negedge r1_1)
    if (!r1_1) q[1] <= 0; else q[1] <= d;
  logic r2_0, r2_1;
  always_ff @(posedge clka or posedge rst)
    if (rst) begin {r2_1, r2_0} <= 2'b11; end
    else begin {r2_1, r2_0} <= {r2_0, 1'b0}; end
  always_ff @(posedge clka or posedge r2_1)
    if (r2_1) q[2] <= 0; else q[2] <= d;
  logic r3_0, r3_1;
  always_ff @(posedge clka or posedge rst)
    if (rst) begin r3_0 <= 1'b1; r3_1 <= 1'b1; end
    else begin r3_0 <= 1'b0; r3_1 <= r3_0; end
  always_ff @(posedge clka or posedge r3_1)
    if (r3_1) q[3] <= 0; else q[3] <= d;
  logic r4_0, r4_1, r4_2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin {r4_2, r4_1, r4_0} <= 3'b000; end
    else begin {r4_2, r4_1, r4_0} <= {r4_1, r4_0, 1'b1}; end
  always_ff @(posedge clka or negedge r4_2)
    if (!r4_2) q[4] <= 0; else q[4] <= d;
  logic r5_0, r5_1, r5_2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin r5_0 <= 1'b0; r5_1 <= 1'b0; r5_2 <= 1'b0; end
    else begin r5_0 <= 1'b1; r5_1 <= r5_0; r5_2 <= r5_1; end
  always_ff @(posedge clka or negedge r5_2)
    if (!r5_2) q[5] <= 0; else q[5] <= d;
  logic r6_0, r6_1, r6_2;
  always_ff @(posedge clka or posedge rst)
    if (rst) begin {r6_2, r6_1, r6_0} <= 3'b111; end
    else begin {r6_2, r6_1, r6_0} <= {r6_1, r6_0, 1'b0}; end
  always_ff @(posedge clka or posedge r6_2)
    if (r6_2) q[6] <= 0; else q[6] <= d;
  logic r7_0, r7_1, r7_2;
  always_ff @(posedge clka or posedge rst)
    if (rst) begin r7_0 <= 1'b1; r7_1 <= 1'b1; r7_2 <= 1'b1; end
    else begin r7_0 <= 1'b0; r7_1 <= r7_0; r7_2 <= r7_1; end
  always_ff @(posedge clka or posedge r7_2)
    if (r7_2) q[7] <= 0; else q[7] <= d;
endmodule
